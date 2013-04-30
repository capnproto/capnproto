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

module Util where

import Data.Char (isUpper, toUpper)
import Data.List (intercalate, isPrefixOf)
import Data.Bits(shiftR, Bits)
import Data.Word(Word8)

--delimit _ [] = ""
--delimit delimiter (h:t) = h ++ concatMap (delimiter ++) t
delimit = intercalate

splitOn :: String -> String -> [String]
splitOn _ "" = [""]
splitOn delimiter text | delimiter `isPrefixOf` text =
    []:splitOn delimiter (drop (length delimiter) text)
splitOn delimiter (c:rest) = let (first:more) = splitOn delimiter rest in (c:first):more

-- Splits "camelCase" into ["camel", "Case"]
splitName :: String -> [String]
splitName (a:rest@(b:_)) | isUpper b = [a]:splitName rest
splitName (a:rest) = case splitName rest of
    firstWord:moreWords -> (a:firstWord):moreWords
    [] -> [[a]]
splitName [] = []

toTitleCase :: String -> String
toTitleCase (a:rest) = toUpper a:rest
toTitleCase [] = []

toUpperCaseWithUnderscores :: String -> String
toUpperCaseWithUnderscores name = delimit "_" $ map (map toUpper) $ splitName name

intToBytes :: (Integral a, Bits a) => a -> Int -> [Word8]
intToBytes i count = map (byte i) [0..(count - 1)] where
    byte :: (Integral a, Bits a) => a -> Int -> Word8
    byte i2 amount = fromIntegral (shiftR i2 (amount * 8))
