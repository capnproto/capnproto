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
import System.Exit(exitFailure, exitSuccess)
import System.IO(hPutStr, stderr)
import System.FilePath(takeDirectory, combine)
import System.Directory(createDirectoryIfMissing, doesDirectoryExist)
import Control.Monad
import Control.Monad.IO.Class(liftIO)
import Control.Exception(IOException, catch)
import Control.Monad.Trans.State(StateT, state, modify, execStateT)
import Prelude hiding (catch)
import Compiler
import Util(delimit)
import Text.Parsec.Pos
import Text.Parsec.Error
import Text.Printf(printf)
import qualified Data.List as List
import qualified Data.Map as Map
import qualified Data.ByteString.Lazy.Char8 as LZ
import Semantics

import CxxGenerator(generateCxx)

type GeneratorFn = FileDesc -> IO [(FilePath, LZ.ByteString)]

generatorFns = Map.fromList [ ("c++", generateCxx) ]

data Opt = OutputOpt String (Maybe GeneratorFn) FilePath
         | VerboseOpt
         | HelpOpt

main :: IO ()
main = do
    let optionDescs =
         [ Option "o" ["output"] (ReqArg parseOutputArg "LANG[:DIR]")
             ("Generate output for language LANG\n\
              \to directory DIR (default: current\n\
              \directory).  LANG may be any of:\n\
              \  " ++ unwords (Map.keys generatorFns))
         , Option "v" ["verbose"] (NoArg VerboseOpt) "Write information about parsed files."
         , Option "h" ["help"] (NoArg HelpOpt) "Print usage info and exit."
         ]
    let usage = usageInfo
         "capnpc [OPTION]... [FILE]...\n\
         \Generate source code based on Cap'n Proto definition FILEs.\n"
         optionDescs
    args <- getArgs
    let (options, files, optErrs) = getOpt Permute optionDescs args
    let langErrs = map (printf "Unknown output language: %s\n")
                       [lang | OutputOpt lang Nothing _ <- options]
    let errs = optErrs ++ langErrs
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

    let isVerbose = not $ null [opt | opt@VerboseOpt <- options]
    let outputs = [(fn, dir) | OutputOpt _ (Just fn) dir <- options]

    let verifyDirectoryExists dir = do
        exists <- doesDirectoryExist dir
        unless exists (do
            hPutStr stderr $ printf "no such directory: %s\n" dir
            exitFailure)
    mapM_ verifyDirectoryExists [dir | (_, dir) <- outputs]

    CompilerState failed _ <-
        execStateT (mapM_ (handleFile outputs isVerbose) files) (CompilerState False Map.empty)
    when failed exitFailure

parseOutputArg :: String -> Opt
parseOutputArg str = case List.elemIndex ':' str of
    Just i -> let (lang, _:dir) = splitAt i str in OutputOpt lang (Map.lookup lang generatorFns) dir
    Nothing -> OutputOpt str (Map.lookup str generatorFns) "."

data ImportState = ImportInProgress | ImportFailed | ImportSucceeded FileDesc
type ImportStateMap = Map.Map String ImportState
data CompilerState = CompilerState Bool ImportStateMap
type CompilerMonad a = StateT CompilerState IO a

importFile :: Bool -> FilePath -> CompilerMonad (Either FileDesc String)
importFile isVerbose filename = do
    fileState <- state (\s@(CompilerState f m) -> case Map.lookup filename m of
        d@Nothing -> (d, CompilerState f (Map.insert filename ImportInProgress m))
        d -> (d, s))

    case fileState of
        Just ImportFailed -> return $ Right "File contained errors."
        Just ImportInProgress -> return $ Right "File cyclically imports itself."
        Just (ImportSucceeded d) -> return $ Left d
        Nothing -> do
            result <- readAndParseFile isVerbose filename
            modify (\(CompilerState f m) -> case result of
                Left desc -> CompilerState f (Map.insert filename (ImportSucceeded desc) m)
                Right _ -> CompilerState True (Map.insert filename ImportFailed m))
            return result

readAndParseFile isVerbose filename = do
    textOrError <- liftIO $ catch (fmap Left $ readFile filename)
        (\ex -> return $ Right $ show (ex :: IOException))

    case textOrError of
        Right err -> return $ Right err
        Left text -> parseFile isVerbose filename text

parseFile isVerbose filename text = do
    let importCallback name = do
            let path = tail $ combine ('/':filename) name
            importFile isVerbose path

    status <- parseAndCompileFile filename text importCallback
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

handleFile :: [(GeneratorFn, FilePath)] -> Bool -> FilePath -> CompilerMonad ()
handleFile outputs isVerbose filename = do
    result <- importFile isVerbose filename

    case result of
        Right _ -> return ()
        Left desc -> do
            let write dir (name, content) = do
                    let outFilename = dir ++ "/" ++ name
                    createDirectoryIfMissing True $ takeDirectory outFilename
                    LZ.writeFile outFilename content
                generate (generatorFn, dir) = do
                    files <- generatorFn desc
                    mapM_ (write dir) files
            liftIO $ mapM_ generate outputs

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
