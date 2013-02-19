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

module Lexer (lexer) where

import Text.Parsec hiding (token, tokens)
import Text.Parsec.String
import Control.Monad (liftM)
import qualified Text.Parsec.Token as T
import Text.Parsec.Language (emptyDef)
import Token

keywords =
    [ (InKeyword, "in")
    , (OfKeyword, "of")
    , (AsKeyword, "as")
    , (WithKeyword, "with")
    , (FromKeyword, "from")
    , (ImportKeyword, "import")
    , (UsingKeyword, "using")
    , (ConstKeyword, "const")
    , (EnumKeyword, "enum")
    , (StructKeyword, "struct")
    , (UnionKeyword, "union")
    , (InterfaceKeyword, "interface")
    , (OptionKeyword, "option")
    ]

languageDef :: T.LanguageDef st
languageDef = emptyDef
    { T.commentLine = "#"
    , T.identStart = letter <|> char '_'
    , T.identLetter = alphaNum <|> char '_'
    , T.reservedNames = [name | (_, name) <- keywords]
    , T.opStart = T.opLetter languageDef
    , T.opLetter = fail "There are no operators."
    }

tokenParser = T.makeTokenParser languageDef

identifier     = T.identifier tokenParser
reserved       = T.reserved tokenParser
symbol         = T.symbol tokenParser
naturalOrFloat = T.naturalOrFloat tokenParser
braces         = T.braces tokenParser
parens         = T.parens tokenParser
brackets       = T.brackets tokenParser
whiteSpace     = T.whiteSpace tokenParser
stringLiteral  = T.stringLiteral tokenParser

keyword :: Parser Token
keyword = foldl1 (<|>) [reserved name >> return t | (t, name) <- keywords]

toLiteral :: Either Integer Double -> Token
toLiteral (Left i) = LiteralInt i
toLiteral (Right d) = LiteralFloat d

located :: Parser t -> Parser (Located t)
located p = do
    pos <- getPosition
    t <- p
    return (Located pos t)

token :: Parser Token
token = keyword
    <|> liftM Identifier         identifier
    <|> liftM ParenthesizedList  (parens (sepBy (many locatedToken) (symbol ",")))
    <|> liftM BracketedList      (brackets (sepBy (many locatedToken) (symbol ",")))
    <|> liftM toLiteral          naturalOrFloat
    <|> liftM LiteralString      stringLiteral
    <|> liftM (const AtSign)     (symbol "@")
    <|> liftM (const Colon)      (symbol ":")
    <|> liftM (const Period)     (symbol ".")
    <|> liftM (const EqualsSign) (symbol "=")
    <|> liftM (const MinusSign)  (symbol "-")
    <|> liftM (const ExclamationPoint) (symbol "!")
    <?> "token"

locatedToken = located token

statementEnd :: Parser (Maybe [Located Statement])
statementEnd = (symbol ";" >>= \_ -> return Nothing)
           <|> (braces (many locatedStatement) >>= \statements -> return (Just statements))

compileStatement :: [Located Token] -> Maybe [Located Statement] -> Statement
compileStatement tokens Nothing = Line tokens
compileStatement tokens (Just statements) = Block tokens statements

statement :: Parser Statement
statement = do
    tokens <- many locatedToken
    end <- statementEnd
    return (compileStatement tokens end)

locatedStatement = located statement

lexer :: Parser [Located Statement]
lexer = do
    whiteSpace
    tokens <- many locatedStatement
    eof
    return tokens
