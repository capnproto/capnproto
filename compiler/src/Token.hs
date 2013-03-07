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

module Token where

import Text.Parsec.Pos (SourcePos, sourceLine, sourceColumn)
import Text.Printf (printf)

data Located t = Located { locatedPos :: SourcePos, locatedValue :: t }

instance Show t => Show (Located t) where
    show (Located pos x) = printf "%d:%d:%s" (sourceLine pos) (sourceColumn pos) (show x)

instance Eq a => Eq (Located a) where
    Located _ a == Located _ b = a == b

instance Ord a => Ord (Located a) where
    compare (Located _ a) (Located _ b) = compare a b

data Token = Identifier String
           | TypeIdentifier String
           | ParenthesizedList [[Located Token]]
           | BracketedList [[Located Token]]
           | LiteralInt Integer
           | LiteralFloat Double
           | LiteralString String
           | AtSign
           | Colon
           | Period
           | EqualsSign
           | MinusSign
           | ExclamationPoint
           | InKeyword
           | OfKeyword    -- We reserve some common, short English words for use as future keywords.
           | OnKeyword
           | AsKeyword
           | WithKeyword
           | FromKeyword
           | ImportKeyword
           | UsingKeyword
           | ConstKeyword
           | EnumKeyword
           | StructKeyword
           | UnionKeyword
           | InterfaceKeyword
           | OptionKeyword
           deriving (Show, Eq)

data Statement = Line [Located Token]
               | Block [Located Token] [Located Statement]
               deriving (Show)
