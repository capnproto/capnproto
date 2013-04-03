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
import Compiler
import Util(delimit)
import Text.Parsec.Pos
import Text.Parsec.Error
import Text.Printf(printf)
import qualified Data.List as List
import qualified Data.ByteString.Lazy.Char8 as LZ

import CxxGenerator

main::IO()
main = do
    args <- getArgs
    let (lang : files) = args
    handleFiles (generatorFnFor lang) files

handleFiles (Right fn) files = mapM_ (handleFile fn) files
handleFiles (Left str) _ = putStrLn str

handleFile generateCode filename = do
    text <- readFile filename
    case parseAndCompileFile filename text of
        Active desc [] -> do
            print desc
            generateCode desc filename

        Active _ e -> mapM_ printError (List.sortBy compareErrors e)
        Failed e -> mapM_ printError (List.sortBy compareErrors e)

generatorFnFor lang = case lang of
    "c++" -> Right (\desc filename -> do
       header <- generateCxxHeader desc
       LZ.writeFile (filename ++ ".h") header
       source <- generateCxxSource desc
       LZ.writeFile (filename ++ ".c++") source)
    _    -> Left "Only c++ is supported for now"

compareErrors a b = compare (errorPos a) (errorPos b)

-- TODO:  This is a fairly hacky way to make showErrorMessages' output not suck.  We could do better
--   by interpreting the error structure ourselves.
printError e = printf "%s:%d:%d: %s\n" f l c m' where
    pos = errorPos e
    f = sourceName pos
    l = sourceLine pos
    c = sourceColumn pos
    m = showErrorMessages "or" "Unknown parse error" "Expected" "Unexpected" "end of expression"
        (errorMessages e)
    m' = delimit "; " (List.filter (not . null) (lines m))
