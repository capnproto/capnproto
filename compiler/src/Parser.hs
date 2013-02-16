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

module Parser (parseFile) where

import Text.Parsec hiding (tokens)
import Token
import Control.Monad (liftM)
import Grammar
import Lexer (lexer)

tokenParser :: (Located Token -> Maybe a) -> Parsec [Located Token] u a
tokenParser = token (show . locatedValue) locatedPos

type TokenParser = Parsec [Located Token] [ParseError]

located :: TokenParser t -> TokenParser (Located t)
located p = do
    input <- getInput
    t <- p
    return (Located (locatedPos (head input)) t)

-- Hmm, boilerplate is not supposed to happen in Haskell.
matchIdentifier t        = case locatedValue t of { (Identifier        v) -> Just v; _ -> Nothing }
matchParenthesizedList t = case locatedValue t of { (ParenthesizedList v) -> Just v; _ -> Nothing }
matchBracketedList t     = case locatedValue t of { (BracketedList     v) -> Just v; _ -> Nothing }
matchLiteralInt t        = case locatedValue t of { (LiteralInt        v) -> Just v; _ -> Nothing }
matchLiteralFloat t      = case locatedValue t of { (LiteralFloat      v) -> Just v; _ -> Nothing }
matchLiteralString t     = case locatedValue t of { (LiteralString     v) -> Just v; _ -> Nothing }
matchSimpleToken expected t = if locatedValue t == expected then Just () else Nothing

identifier = tokenParser matchIdentifier
literalInt = tokenParser matchLiteralInt
literalFloat = tokenParser matchLiteralFloat
literalString = tokenParser matchLiteralString

atSign = tokenParser (matchSimpleToken AtSign)
colon = tokenParser (matchSimpleToken Colon)
period = tokenParser (matchSimpleToken Period)
equalsSign = tokenParser (matchSimpleToken EqualsSign)
importKeyword = tokenParser (matchSimpleToken ImportKeyword)
usingKeyword = tokenParser (matchSimpleToken UsingKeyword)
constKeyword = tokenParser (matchSimpleToken ConstKeyword)
enumKeyword = tokenParser (matchSimpleToken EnumKeyword)
structKeyword = tokenParser (matchSimpleToken StructKeyword)
interfaceKeyword = tokenParser (matchSimpleToken InterfaceKeyword)
optionKeyword = tokenParser (matchSimpleToken OptionKeyword)

parenthesizedList parser = do
    items <- tokenParser matchParenthesizedList
    parseList parser items
bracketedList parser = do
    items <- tokenParser matchBracketedList
    parseList parser items

declNameBase :: TokenParser DeclName
declNameBase = liftM ImportName (importKeyword >> located literalString)
           <|> liftM AbsoluteName (period >> located identifier)
           <|> liftM RelativeName (located identifier)

declName :: TokenParser DeclName
declName = do
    base <- declNameBase
    members <- many (period >> located identifier)
    return (foldl MemberName base members :: DeclName)

typeExpression :: TokenParser TypeExpression
typeExpression = do
    name <- declName
    suffixes <- option [] (parenthesizedList typeExpression)
    return (TypeExpression name suffixes)

topLine :: Maybe [Located Statement] -> TokenParser Declaration
topLine Nothing = optionDecl <|> aliasDecl <|> constantDecl
topLine (Just statements) = typeDecl statements

aliasDecl = do
    usingKeyword
    name <- located identifier
    equalsSign
    target <- declName
    return (AliasDecl name target)

constantDecl = do
    constKeyword
    name <- located identifier
    colon
    typeName <- typeExpression
    equalsSign
    value <- located fieldValue
    return (ConstantDecl name typeName value)

typeDecl statements = enumDecl statements
                  <|> structDecl statements
                  <|> interfaceDecl statements

enumDecl statements = do
    enumKeyword
    name <- located identifier
    children <- parseBlock enumLine statements
    return (EnumDecl name children)

enumLine :: Maybe [Located Statement] -> TokenParser Declaration
enumLine Nothing = optionDecl <|> enumValueDecl []
enumLine (Just statements) = enumValueDecl statements

enumValueDecl statements = do
    name <- located identifier
    equalsSign
    value <- located literalInt
    children <- parseBlock enumValueLine statements
    return (EnumValueDecl name value children)

enumValueLine :: Maybe [Located Statement] -> TokenParser Declaration
enumValueLine Nothing = optionDecl
enumValueLine (Just _) = fail "Blocks not allowed here."

structDecl statements = do
    structKeyword
    name <- located identifier
    children <- parseBlock structLine statements
    return (StructDecl name children)

structLine :: Maybe [Located Statement] -> TokenParser Declaration
structLine Nothing = optionDecl <|> constantDecl <|> fieldDecl []
structLine (Just statements) = typeDecl statements <|> fieldDecl statements

fieldDecl statements = do
    name <- located identifier
    atSign
    ordinal <- located literalInt
    colon
    t <- typeExpression
    value <- optionMaybe (equalsSign >> located fieldValue)
    children <- parseBlock fieldLine statements
    return (FieldDecl name ordinal t value children)

fieldValue = liftM IntegerFieldValue literalInt
         <|> liftM FloatFieldValue literalFloat
         <|> liftM StringFieldValue literalString
         <|> liftM ArrayFieldValue (bracketedList fieldValue)
         <|> liftM RecordFieldValue (parenthesizedList fieldAssignment)

fieldAssignment = do
    name <- identifier
    equalsSign
    value <- fieldValue
    return (name, value)

fieldLine :: Maybe [Located Statement] -> TokenParser Declaration
fieldLine Nothing = optionDecl
fieldLine (Just _) = fail "Blocks not allowed here."

interfaceDecl statements = do
    interfaceKeyword
    name <- located identifier
    children <- parseBlock interfaceLine statements
    return (InterfaceDecl name children)

interfaceLine :: Maybe [Located Statement] -> TokenParser Declaration
interfaceLine Nothing = optionDecl <|> constantDecl <|> methodDecl []
interfaceLine (Just statements) = typeDecl statements <|> methodDecl statements

methodDecl statements = do
    name <- located identifier
    atSign
    ordinal <- located literalInt
    params <- parenthesizedList paramDecl
    t <- typeExpression
    children <- parseBlock methodLine statements
    return (MethodDecl name ordinal params t children)

paramDecl = do
    name <- identifier
    colon
    t <- typeExpression
    value <- optionMaybe (equalsSign >> located fieldValue)
    return (name, t, value)

methodLine :: Maybe [Located Statement] -> TokenParser Declaration
methodLine Nothing = optionDecl
methodLine (Just _) = fail "Blocks not allowed here."

optionDecl = do
    optionKeyword
    name <- declName
    equalsSign
    value <- located fieldValue
    return (OptionDecl name value)

extractErrors :: Either ParseError (a, [ParseError]) -> [ParseError]
extractErrors (Left err) = [err]
extractErrors (Right (_, errors)) = errors

parseList parser items = finish where
    results = map (parseCollectingErrors parser) items
    finish = do
        modifyState (\old -> concat (old:map extractErrors results))
        return [ result | Right (result, _) <- results ]

parseBlock :: (Maybe [Located Statement] -> TokenParser Declaration)
           -> [Located Statement] -> TokenParser [Declaration]
parseBlock parser statements = finish where
    results = map (parseStatement parser) statements
    finish = do
        modifyState (\old -> concat (old:map extractErrors results))
        return [ result | Right (result, _) <- results ]

parseCollectingErrors :: TokenParser a -> [Located Token] -> Either ParseError (a, [ParseError])
parseCollectingErrors parser = runParser parser' [] "" where
    parser' = do
        result <- parser
        eof
        errors <- getState
        return (result, errors)

parseStatement :: (Maybe [Located Statement] -> TokenParser Declaration)
               -> Located Statement
               -> Either ParseError (Declaration, [ParseError])
parseStatement parser (Located _ (Line tokens)) =
    parseCollectingErrors (parser Nothing) tokens
parseStatement parser (Located _ (Block tokens statements)) =
    parseCollectingErrors (parser (Just statements)) tokens

parseFileTokens :: [Located Statement] -> ([Declaration], [ParseError])
parseFileTokens statements = (decls, errors) where
    results = map (parseStatement topLine) statements
    errors = concatMap extractErrors results
    decls = [ result | Right (result, _) <- results ]

parseFile :: String -> String -> ([Declaration], [ParseError])
parseFile filename text = case parse lexer filename text of
    Left e -> ([], [e])
    Right tokens -> parseFileTokens tokens
