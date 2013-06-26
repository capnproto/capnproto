-- Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are met:
--
-- 1. Redistributions of source code must retain the above copyright notice, this
--    list of conditions and the following disclaimer.
-- 2. Redistributions in binary form must reproduce the above copyright notice,
--    this list of conditions and the following disclaimer in the documentation
--    and/or other materials provided with the distribution.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
-- ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
-- WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
-- DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
-- ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
-- (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
-- LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
-- ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
-- (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
-- SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

module Main ( main ) where

import System.Environment
import System.Console.GetOpt
import System.Exit(exitFailure, exitSuccess, ExitCode(..))
import System.IO(hPutStr, stderr, hSetBinaryMode, hClose)
import System.FilePath(takeDirectory)
import System.Directory(createDirectoryIfMissing, doesDirectoryExist, doesFileExist)
import System.Entropy(getEntropy)
import System.Process(createProcess, proc, std_in, cwd, StdStream(CreatePipe), waitForProcess)
import Control.Monad
import Control.Monad.IO.Class(MonadIO, liftIO)
import Control.Exception(IOException, catch)
import Control.Monad.Trans.State(StateT, state, modify, evalStateT)
import qualified Control.Monad.Trans.State as State
import Prelude hiding (catch)
import Compiler
import Util(delimit)
import Text.Parsec.Pos
import Text.Parsec.Error
import Text.Printf(printf)
import qualified Data.List as List
import qualified Data.Map as Map
import qualified Data.ByteString.Lazy.Char8 as LZ
import Data.ByteString(unpack, pack, hPut)
import Data.Word(Word64, Word8)
import Data.Maybe(fromMaybe, catMaybes, mapMaybe)
import Data.Function(on)
import Semantics
import WireFormat(encodeSchema)
import CxxGenerator(generateCxx)
import Paths_capnproto_compiler
import Data.Version(showVersion)

type GeneratorFn = [FileDesc] -> [Word8] -> Map.Map Word64 [Word8] -> IO [(FilePath, LZ.ByteString)]

generatorFns :: Map.Map String GeneratorFn
generatorFns = Map.fromList [ ("c++", generateCxx) ]

data Opt = SearchPathOpt FilePath
         | OutputOpt String GeneratorFn FilePath
         | SrcPrefixOpt String
         | VerboseOpt
         | HelpOpt
         | VersionOpt
         | GenIdOpt

main :: IO ()
main = do
    let optionDescs =
         [ Option "I" ["import-path"] (ReqArg SearchPathOpt "DIR")
             "Search DIR for absolute imports."
         , Option "" ["src-prefix"] (ReqArg SrcPrefixOpt "PREFIX")
             "Prefix directory to strip off of source\n\
             \file names before generating output file\n\
             \names."
         , Option "o" ["output"] (ReqArg parseOutputArg "LANG[:DIR]")
             ("Generate output for language LANG\n\
              \to directory DIR (default: current\n\
              \directory).  LANG may be any of:\n\
              \  " ++ unwords (Map.keys generatorFns) ++ "\n\
              \or a plugin name.")
         , Option "v" ["verbose"] (NoArg VerboseOpt) "Write information about parsed files."
         , Option "i" ["generate-id"] (NoArg GenIdOpt) "Generate a new unique ID."
         , Option "h" ["help"] (NoArg HelpOpt) "Print usage info and exit."
         , Option "" ["version"] (NoArg VersionOpt) "Print version number and exit."
         ]
    let usage = usageInfo
         "capnpc [OPTION]... [FILE]...\n\
         \Generate source code based on Cap'n Proto definition FILEs.\n"
         optionDescs
    args <- getArgs
    let (options, files, errs) = getOpt Permute optionDescs args
    unless (null errs) (do
        mapM_ (hPutStr stderr) errs
        hPutStr stderr usage
        exitFailure)

    when (null options) (do
        hPutStr stderr "Nothing to do.\n"
        hPutStr stderr usage
        exitFailure)

    let isHelp = not $ null [opt | opt@HelpOpt <- options]
    when isHelp (do
        putStr usage
        exitSuccess)

    let isVersion = not $ null [opt | opt@VersionOpt <- options]
    when isVersion (do
        putStr ("Cap'n Proto Compiler " ++ showVersion Paths_capnproto_compiler.version ++ "\n")
        exitSuccess)

    let isGenId = not $ null [opt | opt@GenIdOpt <- options]
    when isGenId (do
        i <- generateId
        _ <- printf "@0x%016x\n" i
        exitSuccess)

    let isVerbose = not $ null [opt | opt@VerboseOpt <- options]
    let outputs = [(fn, dir) | OutputOpt _ fn dir <- options]

    -- TODO(someday):  We should perhaps determine the compiler binary's location and search its
    --   ../include as well.  Also, there should perhaps be a way to tell the compiler not to search
    --   these hard-coded default paths.
    let searchPath = ["/usr/local/include", "/usr/include"] ++
                     [dir | SearchPathOpt dir <- options]
        srcPrefixes = [addTrailingSlash prefix | SrcPrefixOpt prefix <- options]
        addTrailingSlash path =
            if not (null path) && last path /= '/'
                then path ++ "/"
                else path

    let verifyDirectoryExists dir = do
        exists <- doesDirectoryExist dir
        unless exists (do
            hPutStr stderr $ printf "no such directory: %s\n" dir
            exitFailure)
    mapM_ verifyDirectoryExists [dir | (_, dir) <- outputs]

    (failed, requestedFiles, allFiles) <-
        evalStateT (handleFiles isVerbose searchPath files)
                   (CompilerState False Map.empty)

    let (schema, schemaNodes) = encodeSchema requestedFiles allFiles
        toEntry ((i, _), node) = (i, node)
        schemaMap = Map.fromList $ map toEntry schemaNodes
        areDupes (i, _) (j, _) = i == j
        dupes = filter (\x -> length x > 1) $ List.groupBy areDupes
              $ List.sortBy (compare `on` fst) $ map fst schemaNodes

    unless (null dupes) (do
        hPutStr stderr $ concat
            ("Duplicate type / delcaration IDs detected:\n":
             map (concatMap (uncurry $ printf "  @0x%016x %s\n")) dupes)
        hPutStr stderr
            "IDs (16-digit hex strings prefixed with @0x) must be unique.  Sorry I'm not\n\
            \able to be more specific about where the duplicates were seen, but it should\n\
            \be easy enough to grep, right?\n"
        exitFailure)

    mapM_ (doOutput requestedFiles srcPrefixes schema schemaMap) outputs

    when failed exitFailure

handleFiles isVerbose searchPath files = do
    requestedFiles <- liftM catMaybes $ mapM (handleFile isVerbose searchPath) files
    CompilerState failed importMap <- State.get
    return (failed, requestedFiles, [ file | (_, ImportSucceeded file) <- Map.toList importMap ])

parseOutputArg :: String -> Opt
parseOutputArg str = let
    generatorFn lang wd = fromMaybe (callPlugin lang wd) $ Map.lookup lang generatorFns
    in case List.elemIndex ':' str of
        Just i -> let
            (lang, _:dir) = splitAt i str
            in OutputOpt lang (generatorFn lang (Just dir)) dir
        Nothing -> OutputOpt str (generatorFn str Nothing) "."

pluginName lang = if '/' `elem` lang then lang else "capnpc-" ++ lang

callPlugin lang wd _ schema _ = do
    (Just hin, _, _, p) <- createProcess (proc (pluginName lang) [])
        { std_in = CreatePipe, cwd = wd }
    hSetBinaryMode hin True
    hPut hin (pack schema)
    hClose hin
    exitCode <- waitForProcess p
    case exitCode of
        ExitFailure 126 -> do
            _ <- printf "Plugin for language '%s' is not executable.\n" lang
            exitFailure
        ExitFailure 127 -> do
            _ <- printf "No plugin found for language '%s'.\n" lang
            exitFailure
        ExitFailure i -> do
            _ <- printf "Plugin for language '%s' failed with exit code: %d\n" lang i
            exitFailure
        ExitSuccess -> return []

-- As always, here I am, writing my own path manipulation routines, because the ones in the
-- standard lib don't do what I want.
canonicalizePath :: [String] -> [String]
-- An empty string anywhere other than the beginning must be caused by multiple consecutive /'s.
canonicalizePath (a:"":rest) = canonicalizePath (a:rest)
-- An empty string at the beginning means this is an absolute path.
canonicalizePath ("":rest) = "":canonicalizePath rest
-- "." is redundant.
canonicalizePath (".":rest) = canonicalizePath rest
-- ".." at the beginning of the path refers to the parent of the root directory.  Arguably this
-- is illegal but let's at least make sure that "../../foo" doesn't canonicalize to "foo"!
canonicalizePath ("..":rest) = "..":canonicalizePath rest
-- ".." cancels out the previous path component.  Technically this does NOT match what the OS would
-- do in the presence of symlinks:  `foo/bar/..` is NOT `foo` if `bar` is a symlink.  But, in
-- practice, the user almost certainly wants symlinks to behave exactly the same as if the
-- directory had been copied into place.
canonicalizePath (_:"..":rest) = canonicalizePath rest
-- In all other cases, just proceed on.
canonicalizePath (a:rest) = a:canonicalizePath rest
-- All done.
canonicalizePath [] = []

splitPath = loop [] where
    loop part ('/':text) = List.reverse part : loop [] text
    loop part (c:text) = loop (c:part) text
    loop part [] = [List.reverse part]

relativePath from searchPath relative = let
    splitFrom = canonicalizePath $ splitPath from
    splitRelative = canonicalizePath $ splitPath relative
    splitSearchPath = map splitPath searchPath
    -- TODO:  Should we explicitly disallow "/../foo"?
    resultPath = if head splitRelative == ""
        then map (++ tail splitRelative) splitSearchPath
        else [canonicalizePath (init splitFrom ++ splitRelative)]
    in map (List.intercalate "/") resultPath

firstExisting :: [FilePath] -> IO (Maybe FilePath)
firstExisting paths = do
    bools <- mapM doesFileExist paths
    let existing = [path | (True, path) <- zip bools paths]
    return (if null existing then Nothing else Just (head existing))

data ImportState = ImportInProgress | ImportFailed | ImportSucceeded FileDesc
type ImportStateMap = Map.Map String ImportState
data CompilerState = CompilerState Bool ImportStateMap
type CompilerMonad a = StateT CompilerState IO a

importFile :: Bool -> [FilePath] -> FilePath -> CompilerMonad (Either FileDesc String)
importFile isVerbose searchPath filename = do
    fileState <- state (\s@(CompilerState f m) -> case Map.lookup filename m of
        d@Nothing -> (d, CompilerState f (Map.insert filename ImportInProgress m))
        d -> (d, s))

    case fileState of
        Just ImportFailed -> return $ Right "File contained errors."
        Just ImportInProgress -> return $ Right "File cyclically imports itself."
        Just (ImportSucceeded d) -> return $ Left d
        Nothing -> do
            result <- readAndParseFile isVerbose searchPath filename
            modify (\(CompilerState f m) -> case result of
                Left desc -> CompilerState f (Map.insert filename (ImportSucceeded desc) m)
                Right _ -> CompilerState True (Map.insert filename ImportFailed m))
            return result

readAndParseFile isVerbose searchPath filename = do
    textOrError <- liftIO $ catch (fmap Left $ readFile filename)
        (\ex -> return $ Right $ show (ex :: IOException))

    case textOrError of
        Right err -> return $ Right err
        Left text -> parseFile isVerbose searchPath filename text

generateId :: MonadIO m => m Word64
generateId = do
    byteString <- liftIO $ getEntropy 8
    let i | ix < 2^(63::Integer) = ix + 2^(63::Integer)
          | otherwise = ix
        ix = foldl addByte 0 (unpack byteString)
        addByte :: Word64 -> Word8 -> Word64
        addByte b v = b * 256 + fromIntegral v
    return i

parseFile isVerbose searchPath filename text = do
    let importCallback name = do
            let candidates = relativePath filename searchPath name
            maybePath <- liftIO $ firstExisting candidates
            case maybePath of
                Nothing -> return $ Right "File not found."
                Just path -> importFile isVerbose searchPath path

    status <- parseAndCompileFile filename text importCallback generateId
    case status of
        Active desc [] -> do
            when isVerbose (liftIO $ print desc)
            return $ Left desc
        Active _ e -> do
            liftIO $ mapM_ printError (List.sortBy compareErrors e)
            return $ Right "File contained errors."
        Failed e -> do
            liftIO $ mapM_ printError (List.sortBy compareErrors e)
            return $ Right "File contained errors."

handleFile :: Bool -> [FilePath] -> FilePath -> CompilerMonad (Maybe FileDesc)
handleFile isVerbose searchPath filename = do
    result <- importFile isVerbose searchPath filename

    case result of
        Right e -> do
            liftIO $ hPutStr stderr (e ++ "\n")
            return Nothing
        Left desc -> return $ Just desc

doOutput requestedFiles srcPrefixes schema schemaMap output = do
    let write dir (name, content) = do
            let strippedOptions = mapMaybe (flip List.stripPrefix name) srcPrefixes
                stripped = if null strippedOptions then name else
                    List.minimumBy (compare `on` length) strippedOptions
                outFilename = dir ++ "/" ++ stripped
            createDirectoryIfMissing True $ takeDirectory outFilename
            LZ.writeFile outFilename content
        generate (generatorFn, dir) = do
            files <- generatorFn requestedFiles schema schemaMap
            mapM_ (write dir) files
    liftIO $ generate output

compareErrors a b = compare (errorPos a) (errorPos b)

-- TODO:  This is a fairly hacky way to make showErrorMessages' output not suck.  We could do better
--   by interpreting the error structure ourselves.
printError e = hPutStr stderr $ printf "%s:%d:%d: error: %s\n" f l c m' where
    pos = errorPos e
    f = sourceName pos
    l = sourceLine pos
    c = sourceColumn pos
    m = showErrorMessages "or" "Unknown parse error" "Expected" "Unexpected" "end of expression"
        (errorMessages e)
    m' = delimit "; " (List.filter (not . null) (lines m))
