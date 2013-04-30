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

import Data.Generics
import Data.Maybe(fromMaybe, listToMaybe)
import Data.Word(Word64)
import Text.Parsec hiding (tokens)
import Text.Parsec.Error(newErrorMessage, Message(Message))
import Token
import Grammar
import Lexer (lexer)
import Control.Monad.Identity

tokenParser :: (Located Token -> Maybe a) -> Parsec [Located Token] u a
tokenParser = token (tokenErrorString . locatedValue) locatedPos

tokenErrorString (Identifier s) = "identifier \"" ++ s ++ "\""
tokenErrorString (TypeIdentifier s) = "type identifier \"" ++ s ++ "\""
tokenErrorString (ParenthesizedList _) = "parenthesized list"
tokenErrorString (BracketedList _) = "bracketed list"
tokenErrorString (LiteralInt i) = "integer literal " ++ show i
tokenErrorString (LiteralFloat f) = "float literal " ++ show f
tokenErrorString (LiteralString s) = "string literal " ++ show s
tokenErrorString AtSign = "\"@\""
tokenErrorString Colon = "\":\""
tokenErrorString DollarSign = "\"$\""
tokenErrorString Period = "\".\""
tokenErrorString EqualsSign = "\"=\""
tokenErrorString MinusSign = "\"-\""
tokenErrorString Asterisk = "\"*\""
tokenErrorString ExclamationPoint = "\"!\""
tokenErrorString VoidKeyword = "keyword \"void\""
tokenErrorString TrueKeyword = "keyword \"true\""
tokenErrorString FalseKeyword = "keyword \"false\""
tokenErrorString InKeyword = "keyword \"in\""
tokenErrorString OfKeyword = "keyword \"of\""
tokenErrorString OnKeyword = "keyword \"on\""
tokenErrorString AsKeyword = "keyword \"as\""
tokenErrorString WithKeyword = "keyword \"with\""
tokenErrorString FromKeyword = "keyword \"from\""
tokenErrorString ImportKeyword = "keyword \"import\""
tokenErrorString UsingKeyword = "keyword \"using\""
tokenErrorString ConstKeyword = "keyword \"const\""
tokenErrorString EnumKeyword = "keyword \"enum\""
tokenErrorString StructKeyword = "keyword \"struct\""
tokenErrorString UnionKeyword = "keyword \"union\""
tokenErrorString InterfaceKeyword = "keyword \"interface\""
tokenErrorString AnnotationKeyword = "keyword \"annotation\""
tokenErrorString FixedKeyword = "keyword \"fixed\""

type TokenParser = Parsec [Located Token] [ParseError]

located :: TokenParser t -> TokenParser (Located t)
located p = do
    input <- getInput
    t <- p
    return (Located (locatedPos (head input)) t)

matchUnary :: (Data a, Data b) => (a -> b) -> Located b -> Maybe a
matchUnary c t = if toConstr (c undefined) == toConstr v
        then Just $ gmapQi 0 (undefined `mkQ` id) v
        else Nothing
    where v = locatedValue t
matchIdentifier = matchUnary Identifier
matchTypeIdentifier = matchUnary TypeIdentifier
matchLiteralBool t = case locatedValue t of
    TrueKeyword -> Just True
    FalseKeyword -> Just False
    _ -> Nothing
matchSimpleToken expected t = if locatedValue t == expected then Just () else Nothing

matchLiteralId :: Located Token -> Maybe Word64
matchLiteralId (Located _ (LiteralInt i))
    | i >= (2^(63 :: Integer)) &&
      i <  (2^(64 :: Integer))
    = Just (fromIntegral i)
matchLiteralId _ = Nothing

varIdentifier = tokenParser matchIdentifier
            <|> (tokenParser matchTypeIdentifier >>=
                 fail "Non-type identifiers must start with lower-case letter.")
            <?> "identifier"
typeIdentifier = tokenParser matchTypeIdentifier
             <|> (tokenParser matchIdentifier >>=
                  fail "Type identifiers must start with upper-case letter.")
             <?> "type identifier"
anyIdentifier = tokenParser matchIdentifier
            <|> tokenParser matchTypeIdentifier
            <?> "identifier"

literalInt = tokenParser (matchUnary LiteralInt) <?> "integer"
literalFloat = tokenParser (matchUnary LiteralFloat) <?> "floating-point number"
literalString = tokenParser (matchUnary LiteralString) <?> "string"
literalId = tokenParser matchLiteralId <?> "id (generate using capnpc -i)"
literalBool = tokenParser matchLiteralBool <?> "boolean"
literalVoid = tokenParser (matchSimpleToken VoidKeyword) <?> "\"void\""

atSign = tokenParser (matchSimpleToken AtSign) <?> "\"@\""
colon = tokenParser (matchSimpleToken Colon) <?> "\":\""
dollarSign = tokenParser (matchSimpleToken DollarSign) <?> "\"$\""
period = tokenParser (matchSimpleToken Period) <?> "\".\""
equalsSign = tokenParser (matchSimpleToken EqualsSign) <?> "\"=\""
minusSign = tokenParser (matchSimpleToken MinusSign) <?> "\"-\""
asterisk = tokenParser (matchSimpleToken Asterisk) <?> "\"*\""
importKeyword = tokenParser (matchSimpleToken ImportKeyword) <?> "\"import\""
usingKeyword = tokenParser (matchSimpleToken UsingKeyword) <?> "\"using\""
constKeyword = tokenParser (matchSimpleToken ConstKeyword) <?> "\"const\""
enumKeyword = tokenParser (matchSimpleToken EnumKeyword) <?> "\"enum\""
structKeyword = tokenParser (matchSimpleToken StructKeyword) <?> "\"struct\""
unionKeyword = tokenParser (matchSimpleToken UnionKeyword) <?> "\"union\""
interfaceKeyword = tokenParser (matchSimpleToken InterfaceKeyword) <?> "\"interface\""
annotationKeyword = tokenParser (matchSimpleToken AnnotationKeyword) <?> "\"annotation\""
fixedKeyword = tokenParser (matchSimpleToken FixedKeyword) <?> "\"fixed\""

exactIdentifier s = tokenParser (matchSimpleToken $ Identifier s) <?> "\"" ++ s ++ "\""

parenthesizedList parser = do
    items <- tokenParser (matchUnary ParenthesizedList)
    parseList parser items
parenthesized parser = do
    items <- tokenParser (matchUnary ParenthesizedList)
    unless (length items == 1) (fail "Expected exactly one item in parentheses.")
    [result] <- parseList parser items
    return result
bracketedList parser = do
    items <- tokenParser (matchUnary BracketedList)
    parseList parser items

declNameBase :: TokenParser DeclName
declNameBase = liftM ImportName (importKeyword >> located literalString)
           <|> liftM AbsoluteName (period >> located anyIdentifier)
           <|> liftM RelativeName (located anyIdentifier)

declName :: TokenParser DeclName
declName = do
    base <- declNameBase
    members <- many (period >> located anyIdentifier)
    return (foldl MemberName base members :: DeclName)

typeParameter :: TokenParser TypeParameter
typeParameter = liftM TypeParameterInteger literalInt
            <|> liftM TypeParameterType typeExpression

typeExpression :: TokenParser TypeExpression
typeExpression = do
    name <- declName
    suffixes <- option [] (parenthesizedList typeParameter)
    return (TypeExpression name suffixes)

nameWithOrdinal :: TokenParser (Located String, Located Integer)
nameWithOrdinal = do
    name <- located varIdentifier
    atSign
    ordinal <- located literalInt
    return (name, ordinal)

declId = atSign >> literalId

annotation :: TokenParser Annotation
annotation = do
    dollarSign
    name <- declName
    value <- located (try (parenthesized fieldValue)
                  <|> liftM RecordFieldValue (parenthesizedList fieldAssignment)
                  <|> return VoidFieldValue)
    return (Annotation name value)

data TopLevelDecl = TopLevelDecl Declaration
                  | TopLevelAnnotation Annotation
                  | TopLevelId (Located Word64)

topLine :: Maybe [Located Statement] -> TokenParser TopLevelDecl
topLine Nothing = liftM TopLevelId (located declId)
              <|> liftM TopLevelDecl (usingDecl <|> constantDecl <|> annotationDecl)
              <|> liftM TopLevelAnnotation annotation
topLine (Just statements) = liftM TopLevelDecl $ typeDecl statements

usingDecl = do
    usingKeyword
    maybeName <- optionMaybe $ try (do
        name <- located typeIdentifier
        equalsSign
        return name)
    target <- declName
    name <- let
        inferredName = case target of
            AbsoluteName s -> return s
            RelativeName s -> return s
            ImportName _ -> fail "When 'using' an 'import' you must specify a name, e.g.: \
                                 \using Foo = import \"foo.capnp\";"
            MemberName _ s -> return s
        in maybe inferredName return maybeName
    return (UsingDecl name target)

constantDecl = do
    constKeyword
    name <- located varIdentifier
    colon
    typeName <- typeExpression
    equalsSign
    value <- located fieldValue
    annotations <- many annotation
    return (ConstantDecl name typeName annotations value)

typeDecl statements = enumDecl statements
                  <|> structDecl statements
                  <|> interfaceDecl statements

enumDecl statements = do
    enumKeyword
    name <- located typeIdentifier
    typeId <- optionMaybe $ located declId
    annotations <- many annotation
    children <- parseBlock enumLine statements
    return (EnumDecl name typeId annotations children)

enumLine :: Maybe [Located Statement] -> TokenParser Declaration
enumLine Nothing = enumerantDecl
enumLine (Just _) = fail "Blocks not allowed here."

enumerantDecl = do
    (name, value) <- nameWithOrdinal
    annotations <- many annotation
    return (EnumerantDecl name value annotations)

structDecl statements = do
    structKeyword
    name <- located typeIdentifier
    typeId <- optionMaybe $ located declId
    fixed <- optionMaybe fixedSpec
    annotations <- many annotation
    children <- parseBlock structLine statements
    return (StructDecl name typeId fixed annotations children)

fixedSpec = do
    fixedKeyword
    Located pos sizes <- located $ parenthesizedList fixedSize
    (dataSize, pointerSize) <- foldM combineFixedSizes (Nothing, Nothing) sizes
    return $ Located pos (fromMaybe 0 dataSize, fromMaybe 0 pointerSize)

data FixedSize = FixedData Integer | FixedPointers Integer

combineFixedSizes :: (Maybe Integer, Maybe Integer) -> FixedSize
                  -> TokenParser (Maybe Integer, Maybe Integer)
combineFixedSizes (Nothing, p) (FixedData d) = return (Just d, p)
combineFixedSizes (Just _, _) (FixedData _) =
    fail "Multiple data section size specifications."
combineFixedSizes (d, Nothing) (FixedPointers p) = return (d, Just p)
combineFixedSizes (_, Just _) (FixedPointers _) =
    fail "Multiple pointer section size specifications."

fixedSize = do
    size <- literalInt
    -- We do not allow single-bit structs because most CPUs cannot address bits.
    (exactIdentifier "bytes" >> return (FixedData (8 * size)))
        <|> (exactIdentifier "pointers" >> return (FixedPointers size))
        <?> "\"bytes\" or \"pointers\""

structLine :: Maybe [Located Statement] -> TokenParser Declaration
structLine Nothing = usingDecl <|> constantDecl <|> fieldDecl <|> annotationDecl
structLine (Just statements) = typeDecl statements <|> unionDecl statements <|> unionDecl statements

unionDecl statements = do
    (name, ordinal) <- nameWithOrdinal
    unionKeyword
    annotations <- many annotation
    children <- parseBlock unionLine statements
    return (UnionDecl name ordinal annotations children)

unionLine :: Maybe [Located Statement] -> TokenParser Declaration
unionLine Nothing = fieldDecl
unionLine (Just _) = fail "Blocks not allowed here."

fieldDecl = do
    (name, ordinal) <- nameWithOrdinal
    colon
    t <- typeExpression
    value <- optionMaybe (equalsSign >> located fieldValue)
    annotations <- many annotation
    return (FieldDecl name ordinal t annotations value)

negativeFieldValue = liftM (IntegerFieldValue . negate) literalInt
                 <|> liftM (FloatFieldValue . negate) literalFloat
                 <|> (exactIdentifier "inf" >> return (FloatFieldValue (-1.0 / 0.0)))

fieldValue = (literalVoid >> return VoidFieldValue)
         <|> liftM BoolFieldValue literalBool
         <|> liftM IntegerFieldValue literalInt
         <|> liftM FloatFieldValue literalFloat
         <|> liftM StringFieldValue literalString
         <|> enumOrUnionFieldValue
         <|> liftM ListFieldValue (bracketedList (located fieldValue))
         <|> liftM RecordFieldValue (parenthesizedList fieldAssignment)
         <|> (minusSign >> negativeFieldValue)
         <?> "default value"

enumOrUnionFieldValue = do
    name <- varIdentifier
    liftM (UnionFieldValue name) (try (parenthesized fieldValue))
        <|> liftM (UnionFieldValue name . RecordFieldValue) (parenthesizedList fieldAssignment)
        <|> return (IdentifierFieldValue name)

fieldAssignment = do
    name <- located varIdentifier
    equalsSign
    value <- located fieldValue
    return (name, value)

interfaceDecl statements = do
    interfaceKeyword
    name <- located typeIdentifier
    typeId <- optionMaybe $ located declId
    annotations <- many annotation
    children <- parseBlock interfaceLine statements
    return (InterfaceDecl name typeId annotations children)

interfaceLine :: Maybe [Located Statement] -> TokenParser Declaration
interfaceLine Nothing = usingDecl <|> constantDecl <|> methodDecl <|> annotationDecl
interfaceLine (Just statements) = typeDecl statements

methodDecl = do
    (name, ordinal) <- nameWithOrdinal
    params <- parenthesizedList paramDecl
    colon
    t <- typeExpression
    annotations <- many annotation
    return (MethodDecl name ordinal params t annotations)

paramDecl = do
    name <- varIdentifier
    colon
    t <- typeExpression
    value <- optionMaybe (equalsSign >> located fieldValue)
    annotations <- many annotation
    return (ParamDecl name t annotations value)

annotationDecl = do
    annotationKeyword
    name <- located varIdentifier
    annId <- optionMaybe $ located declId
    targets <- try (parenthesized asterisk >> return allAnnotationTargets)
           <|> parenthesizedList annotationTarget
    colon
    t <- typeExpression
    annotations <- many annotation
    return (AnnotationDecl name annId t annotations targets)
allAnnotationTargets = [minBound::AnnotationTarget .. maxBound::AnnotationTarget]

annotationTarget = (exactIdentifier "file" >> return FileAnnotation)
               <|> (constKeyword >> return ConstantAnnotation)
               <|> (enumKeyword >> return EnumAnnotation)
               <|> (exactIdentifier "enumerant" >> return EnumerantAnnotation)
               <|> (structKeyword >> return StructAnnotation)
               <|> (exactIdentifier "field" >> return FieldAnnotation)
               <|> (unionKeyword >> return UnionAnnotation)
               <|> (interfaceKeyword >> return InterfaceAnnotation)
               <|> (exactIdentifier "method" >> return MethodAnnotation)
               <|> (exactIdentifier "parameter" >> return ParamAnnotation)
               <|> (annotationKeyword >> return AnnotationAnnotation)

extractErrors :: Either ParseError (a, [ParseError]) -> [ParseError]
extractErrors (Left err) = [err]
extractErrors (Right (_, errors)) = errors

parseList parser items = do
    let results = map (parseCollectingErrors parser) items
    modifyState (\old -> concat (old:map extractErrors results))
    return [ result | Right (result, _) <- results ]

parseBlock :: (Maybe [Located Statement] -> TokenParser Declaration)
           -> [Located Statement] -> TokenParser [Declaration]
parseBlock parser statements = do
    let results = map (parseStatement parser) statements
    modifyState (\old -> concat (old:map extractErrors results))
    return [ result | Right (result, _) <- results ]

parseCollectingErrors :: TokenParser a -> TokenSequence
                      -> Either ParseError (a, [ParseError])
parseCollectingErrors parser tokenSequence = runParser parser' [] "" tokens where
    TokenSequence tokens endPos = tokenSequence
    parser' = do
        -- Work around Parsec bug:  Text.Parsec.Print.token is supposed to produce a parser that
        -- sets the position by using the provided function to extract it from each token.  However,
        -- it doesn't bother to call this function for the *first* token, only subsequent tokens.
        -- The first token is always assumed to be at 1:1.  To fix this, set it manually.
        --
        -- TODO:  There's still a problem when a parse error occurs at end-of-input:  Parsec will
        --   report the error at the location of the previous token.
        setPosition (case tokens of
            Located pos2 _:_ -> pos2
            [] -> endPos)

        result <- parser
        eof
        errors <- getState
        return (result, errors)

parseStatement :: (Maybe [Located Statement] -> TokenParser a)
               -> Located Statement
               -> Either ParseError (a, [ParseError])
parseStatement parser (Located _ (Line tokens)) =
    parseCollectingErrors (parser Nothing) tokens
parseStatement parser (Located _ (Block tokens statements)) =
    parseCollectingErrors (parser (Just statements)) tokens

parseFileTokens :: [Located Statement]
                -> (Maybe (Located Word64), [Declaration], [Annotation], [ParseError])
parseFileTokens statements = (fileId, decls, annotations, errors) where
    results :: [Either ParseError (TopLevelDecl, [ParseError])]
    results = map (parseStatement topLine) statements
    errors = concatMap extractErrors results ++ idErrors
    decls = [ decl | Right (TopLevelDecl decl, _) <- results ]
    annotations = [ ann | Right (TopLevelAnnotation ann, _) <- results ]
    ids = [ i | Right (TopLevelId i, _) <- results ]
    fileId = listToMaybe ids
    idErrors | length ids <= 1 = []
             | otherwise = map makeDupeIdError ids
    makeDupeIdError (Located pos _) =
        newErrorMessage (Message "File declares multiple ids.") pos

parseFile :: String -> String
          -> (Maybe (Located Word64), [Declaration], [Annotation], [ParseError])
parseFile filename text = case parse lexer filename text of
    Left e -> (Nothing, [], [], [e])
    Right statements -> parseFileTokens statements
