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
import Control.Monad (liftM, when)
import qualified Text.Parsec.Token as T
import Text.Parsec.Language (emptyDef)
import Token
import Data.Char (isUpper, isLower)

keywords =
    [ (VoidKeyword, "void")
    , (TrueKeyword, "true")
    , (FalseKeyword, "false")
    , (InKeyword, "in")
    , (OfKeyword, "of")
    , (OnKeyword, "on")
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
    , (AnnotationKeyword, "annotation")
--    , (FixedKeyword, "fixed")     -- Inlines have been disabled because they were too complicated.
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

rawIdentifier  = T.identifier tokenParser
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

isTypeName (c:_) = isUpper c
isTypeName _ = False

hasUppercaseAcronym (a:rest@(b:c:_)) =
    (isUpper a && isUpper b && not (isLower c)) || hasUppercaseAcronym rest
hasUppercaseAcronym (a:b:[]) = isUpper a && isUpper b
hasUppercaseAcronym _ = False

identifier :: Parser Token
identifier = do
    text <- rawIdentifier
    when (elem '_' text) $
        fail "Identifiers containing underscores are reserved for the implementation.  Use \
             \camelCase style for multi-word names."
    when (hasUppercaseAcronym text) $
        fail "Wrong style:  Only the first letter of an acronym should be capitalized.  \
             \Consistent style is necessary to allow code generators to sanely translate \
             \names into the target language's preferred style."
    return (if isTypeName text then TypeIdentifier text else Identifier text)

tokenSequence = do
    tokens <- many1 locatedToken
    endPos <- getPosition
    return (TokenSequence tokens endPos)

token :: Parser Token
token = keyword
    <|> identifier
    <|> liftM ParenthesizedList   (parens (sepBy tokenSequence (symbol ",")))
    <|> liftM BracketedList       (brackets (sepBy tokenSequence (symbol ",")))
    <|> liftM toLiteral           naturalOrFloat
    <|> liftM LiteralString       stringLiteral
    <|> liftM (const AtSign)      (symbol "@")
    <|> liftM (const Colon)       (symbol ":")
    <|> liftM (const DollarSign)  (symbol "$")
    <|> liftM (const Period)      (symbol ".")
    <|> liftM (const EqualsSign)  (symbol "=")
    <|> liftM (const MinusSign)   (symbol "-")
    <|> liftM (const Asterisk)    (symbol "*")
    <|> liftM (const ExclamationPoint) (symbol "!")
    <?> "token"

locatedToken = located token

statementEnd :: Parser (Maybe [Located Statement])
statementEnd = (symbol ";" >>= \_ -> return Nothing)
           <|> (braces (many locatedStatement) >>= \statements -> return (Just statements))

compileStatement :: TokenSequence -> Maybe [Located Statement] -> Statement
compileStatement tokens Nothing = Line tokens
compileStatement tokens (Just statements) = Block tokens statements

statement :: Parser Statement
statement = do
    tokens <- tokenSequence
    end <- statementEnd
    return (compileStatement tokens end)

locatedStatement = located statement

lexer :: Parser [Located Statement]
lexer = do
    whiteSpace
    tokens <- many locatedStatement
    eof
    return tokens
