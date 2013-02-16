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

module Compiler where

import Grammar
import Semantics
import Token(Located(Located))
import Parser(parseFile)
import qualified Data.Map as Map
import Text.Parsec.Pos(SourcePos, newPos)
import Text.Parsec.Error(ParseError, newErrorMessage, Message(Message, Expect))
import Text.Printf(printf)

------------------------------------------------------------------------------------------
-- Error helpers
------------------------------------------------------------------------------------------

data Status a = Active a [ParseError]
              | Failed [ParseError]
              deriving(Show)

statusErrors (Active _ e) = e
statusErrors (Failed e) = e

statusAddErrors errs (Active x e) = Active x (e ++ errs)
statusAddErrors errs (Failed e)   = Failed (e ++ errs)

instance Functor Status where
    fmap f (Active x e) = Active (f x) e
    fmap _ (Failed e) = Failed e

instance Monad Status where
    (Active x e) >>= k = statusAddErrors e (k x)
    (Failed e)   >>= _ = Failed e

    -- If the result is ignored, we can automatically recover.
    (Active _ e) >>  k = statusAddErrors e k
    (Failed e)   >>  k = statusAddErrors e k

    return x = Active x []
    fail     = makeError (newPos "?" 0 0)

recover :: a -> Status a -> Status a
recover _ (Active x e) = Active x e
recover x (Failed e)   = Active x e

succeed :: a -> Status a
succeed x = Active x []

makeError pos message = Failed [ newErrorMessage (Message message) pos ]
makeExpectError pos message = Failed [ newErrorMessage (Expect message) pos ]

maybeError :: Maybe t -> SourcePos -> String -> Status t
maybeError (Just x) _ _ = succeed x
maybeError Nothing pos message = makeError pos message

declNamePos (AbsoluteName (Located pos _)) = pos
declNamePos (RelativeName (Located pos _)) = pos
declNamePos (ImportName (Located pos _)) = pos
declNamePos (MemberName _ (Located pos _)) = pos

declNameString (AbsoluteName (Located _ n)) = n
declNameString (RelativeName (Located _ n)) = n
declNameString (ImportName (Located _ n)) = n
declNameString (MemberName _ (Located _ n)) = n

-- Trick for feeding a function's own result back in as a parameter, taking advantage of
-- lazy evaluation.  If the function returns a Failed status, then it must do so withous using
-- its parameter.
feedback :: (a -> Status a) -> Status a
feedback f = status where
    status = f result
    result = case status of
        Active x _ -> x
        Failed _ -> undefined

statusToMaybe (Active x _) = Just x
statusToMaybe (Failed _) = Nothing

------------------------------------------------------------------------------------------
-- Symbol lookup
------------------------------------------------------------------------------------------

-- | Look up a direct member of a descriptor by name.
descMember name (DescFile      d) = lookupMember name (fileMemberMap d)
descMember name (DescEnum      d) = lookupMember name (enumMemberMap d)
descMember name (DescClass     d) = lookupMember name (classMemberMap d)
descMember name (DescInterface d) = lookupMember name (interfaceMemberMap d)
descMember name (DescAlias     d) = descMember name (aliasTarget d)
descMember _ _ = Nothing

-- | Lookup the given name in the scope of the given descriptor.
lookupDesc :: Desc -> DeclName -> Status Desc

-- For a member, look up the parent, then apply descMember.
lookupDesc scope (MemberName parentName (Located pos name)) = do
    p <- lookupDesc scope parentName
    maybeError (descMember name p) pos
        (printf "'%s' is not defined in '%s'." name (declNameString parentName))

-- Implement absolute, relative, and import names on the file scope by just checking the appropriate
-- map.  There is not parent scope to which to recurse.
lookupDesc (DescFile desc) (AbsoluteName (Located pos name)) =
    maybeError (lookupMember name (fileMemberMap desc)) pos
        (printf "'%s' is not defined." name)
lookupDesc (DescFile desc) (RelativeName (Located pos name)) = result where
    maybeResult = case lookupMember name (fileMemberMap desc) of
        Just x -> Just x
        Nothing -> Map.lookup name builtinTypeMap
    result = maybeError maybeResult pos
        (printf "'%s' is not defined." name)
lookupDesc (DescFile desc) (ImportName (Located pos name)) =
    maybeError (fmap DescFile (Map.lookup name (fileImportMap desc))) pos
        (printf "'%s' was not in the import table." name)

-- Implement other relative names by first checking the current scope, then the parent.
lookupDesc scope (RelativeName (Located pos name)) =
    case descMember name scope of
        Just m -> succeed m
        Nothing -> lookupDesc (descParent scope) (RelativeName (Located pos name))

-- For non-relative names on non-file scopes, just recurse out to parent scope.
lookupDesc scope name = lookupDesc (descParent scope) name

builtinTypeMap :: Map.Map String Desc
builtinTypeMap = Map.fromList
    ([(builtinTypeName t, DescBuiltinType t) | t <- builtinTypes] ++
     [("List", DescBuiltinList)])

------------------------------------------------------------------------------------------

fromIntegerChecked :: Integral a => SourcePos -> Integer -> Status a
fromIntegerChecked pos x = result where
    unchecked = fromInteger x
    result = if toInteger unchecked == x
        then succeed unchecked
        else makeError pos "Integer out of range for type."

compileValue _ (BuiltinType BuiltinVoid) VoidFieldValue = succeed VoidDesc
compileValue _ (BuiltinType BuiltinBool) (BoolFieldValue x) = succeed (BoolDesc x)
compileValue pos (BuiltinType BuiltinInt8) (IntegerFieldValue x) = fmap Int8Desc (fromIntegerChecked pos x)
compileValue pos (BuiltinType BuiltinInt16) (IntegerFieldValue x) = fmap Int16Desc (fromIntegerChecked pos x)
compileValue pos (BuiltinType BuiltinInt32) (IntegerFieldValue x) = fmap Int32Desc (fromIntegerChecked pos x)
compileValue pos (BuiltinType BuiltinInt64) (IntegerFieldValue x) = fmap Int64Desc (fromIntegerChecked pos x)
compileValue pos (BuiltinType BuiltinUInt8) (IntegerFieldValue x) = fmap UInt8Desc (fromIntegerChecked pos x)
compileValue pos (BuiltinType BuiltinUInt16) (IntegerFieldValue x) = fmap UInt16Desc (fromIntegerChecked pos x)
compileValue pos (BuiltinType BuiltinUInt32) (IntegerFieldValue x) = fmap UInt32Desc (fromIntegerChecked pos x)
compileValue pos (BuiltinType BuiltinUInt64) (IntegerFieldValue x) = fmap UInt64Desc (fromIntegerChecked pos x)
compileValue _ (BuiltinType BuiltinFloat32) (FloatFieldValue x) = succeed (Float32Desc (realToFrac x))
compileValue _ (BuiltinType BuiltinFloat64) (FloatFieldValue x) = succeed (Float64Desc x)
compileValue _ (BuiltinType BuiltinFloat32) (IntegerFieldValue x) = succeed (Float32Desc (realToFrac x))
compileValue _ (BuiltinType BuiltinFloat64) (IntegerFieldValue x) = succeed (Float64Desc (realToFrac x))
compileValue _ (BuiltinType BuiltinText) (StringFieldValue x) = succeed (TextDesc x)
compileValue _ (BuiltinType BuiltinBytes) (StringFieldValue x) =
    succeed (BytesDesc (map (fromIntegral . fromEnum) x))

compileValue pos (BuiltinType BuiltinVoid) _ = makeError pos "Void fields cannot have values."
compileValue pos (BuiltinType BuiltinBool) _ = makeExpectError pos "boolean"
compileValue pos (BuiltinType BuiltinInt8) _ = makeExpectError pos "integer"
compileValue pos (BuiltinType BuiltinInt16) _ = makeExpectError pos "integer"
compileValue pos (BuiltinType BuiltinInt32) _ = makeExpectError pos "integer"
compileValue pos (BuiltinType BuiltinInt64) _ = makeExpectError pos "integer"
compileValue pos (BuiltinType BuiltinUInt8) _ = makeExpectError pos "integer"
compileValue pos (BuiltinType BuiltinUInt16) _ = makeExpectError pos "integer"
compileValue pos (BuiltinType BuiltinUInt32) _ = makeExpectError pos "integer"
compileValue pos (BuiltinType BuiltinUInt64) _ = makeExpectError pos "integer"
compileValue pos (BuiltinType BuiltinFloat32) _ = makeExpectError pos "number"
compileValue pos (BuiltinType BuiltinFloat64) _ = makeExpectError pos "number"
compileValue pos (BuiltinType BuiltinText) _ = makeExpectError pos "string"
compileValue pos (BuiltinType BuiltinBytes) _ = makeExpectError pos "string"

compileValue pos (EnumType _) _ = makeError pos "Unimplemented: enum default values"
compileValue pos (ClassType _) _ = makeError pos "Unimplemented: class default values"
compileValue pos (InterfaceType _) _ = makeError pos "Interfaces can't have default values."
compileValue pos (ListType _) _ = makeError pos "Unimplemented: array default values"

makeFileMemberMap :: FileDesc -> Map.Map String Desc
makeFileMemberMap desc = Map.fromList allMembers where
    allMembers = [ (aliasName     m, DescAlias     m) | m <- fileAliases    desc ]
              ++ [ (constantName  m, DescConstant  m) | m <- fileConstants  desc ]
              ++ [ (enumName      m, DescEnum      m) | m <- fileEnums      desc ]
              ++ [ (className     m, DescClass     m) | m <- fileClasses    desc ]
              ++ [ (interfaceName m, DescInterface m) | m <- fileInterfaces desc ]

descAsType _ (DescEnum desc) = succeed (EnumType desc)
descAsType _ (DescClass desc) = succeed (ClassType desc)
descAsType _ (DescInterface desc) = succeed (InterfaceType desc)
descAsType _ (DescBuiltinType desc) = succeed (BuiltinType desc)
descAsType name (DescAlias desc) = descAsType name (aliasTarget desc)
descAsType name DescBuiltinList = makeError (declNamePos name) message where
            message = printf "'List' requires exactly one type parameter." (declNameString name)
descAsType name _ = makeError (declNamePos name) message where
            message = printf "'%s' is not a type." (declNameString name)

compileType :: Desc -> TypeExpression -> Status TypeDesc
compileType scope (TypeExpression n []) = do
    desc <- lookupDesc scope n
    descAsType n desc
compileType scope (TypeExpression n (param:moreParams)) = do
    desc <- lookupDesc scope n
    case desc of
        DescBuiltinList ->
            if null moreParams
                then fmap ListType (compileType scope param)
                else makeError (declNamePos n) "'List' requires exactly one type parameter."
        _ -> makeError (declNamePos n) "Only the type 'List' can have type parameters."

data CompiledDecl = CompiledMember String (Status Desc)
                  | CompiledOption (Status OptionAssignmentDesc)

compiledErrors (CompiledMember _ status) = statusErrors status
compiledErrors (CompiledOption status) = statusErrors status

compileChildDecls :: Desc -> [Declaration] -> Status ([Desc], MemberMap, OptionMap)
compileChildDecls desc decls = Active (members, memberMap, options) errors where
    compiledDecls = map (compileDecl desc) decls
    memberMap = Map.fromList memberPairs
    members = [member | (_, Just member) <- memberPairs]
    memberPairs = [(name, statusToMaybe status) | CompiledMember name status <- compiledDecls]
    options = Map.fromList [(optionName (optionAssignmentOption o), o)
                           | CompiledOption (Active o _) <- compiledDecls]
    errors = concatMap compiledErrors compiledDecls

doAll statuses = Active [x | (Active x _) <- statuses] (concatMap statusErrors statuses)

compileDecl scope (AliasDecl (Located _ name) target) =
    CompiledMember name (do
        targetDesc <- lookupDesc scope target
        return (DescAlias AliasDesc
            { aliasName = name
            , aliasParent = scope
            , aliasTarget = targetDesc
            }))

compileDecl scope (ConstantDecl (Located _ name) t (Located valuePos value)) =
    CompiledMember name (do
        typeDesc <- compileType scope t
        valueDesc <- compileValue valuePos typeDesc value
        return (DescConstant ConstantDesc
            { constantName = name
            , constantParent = scope
            , constantType = typeDesc
            , constantValue = valueDesc
            }))

compileDecl scope (EnumDecl (Located _ name) decls) =
    CompiledMember name (feedback (\desc -> do
        (members, memberMap, options) <- compileChildDecls desc decls
        return (DescEnum EnumDesc
            { enumName = name
            , enumParent = scope
            , enumValues = [d | DescEnumValue d <- members]
            , enumOptions = options
            , enumMembers = members
            , enumMemberMap = memberMap
            })))

compileDecl scope (EnumValueDecl (Located _ name) (Located _ number) decls) =
    CompiledMember name (feedback (\desc -> do
        (_, _, options) <- compileChildDecls desc decls
        return (DescEnumValue EnumValueDesc
            { enumValueName = name
            , enumValueParent = scope
            , enumValueNumber = number
            , enumValueOptions = options
            })))

compileDecl scope (ClassDecl (Located _ name) decls) =
    CompiledMember name (feedback (\desc -> do
        (members, memberMap, options) <- compileChildDecls desc decls
        return (DescClass ClassDesc
            { className = name
            , classParent = scope
            , classFields           = [d | DescField     d <- members]
            , classNestedAliases    = [d | DescAlias     d <- members]
            , classNestedConstants  = [d | DescConstant  d <- members]
            , classNestedEnums      = [d | DescEnum      d <- members]
            , classNestedClasses    = [d | DescClass     d <- members]
            , classNestedInterfaces = [d | DescInterface d <- members]
            , classOptions = options
            , classMembers = members
            , classMemberMap = memberMap
            })))

compileDecl scope (FieldDecl (Located _ name) (Located _ number) typeExp defaultValue decls) =
    CompiledMember name (feedback (\desc -> do
        typeDesc <- compileType scope typeExp
        defaultDesc <- case defaultValue of
            Just (Located pos value) -> fmap Just (compileValue pos typeDesc value)
            Nothing -> return Nothing
        (_, _, options) <- compileChildDecls desc decls
        return (DescField FieldDesc
            { fieldName = name
            , fieldParent = scope
            , fieldNumber = number
            , fieldType = typeDesc
            , fieldDefaultValue = defaultDesc
            , fieldOptions = options
            })))

compileDecl scope (InterfaceDecl (Located _ name) decls) =
    CompiledMember name (feedback (\desc -> do
        (members, memberMap, options) <- compileChildDecls desc decls
        return (DescInterface InterfaceDesc
            { interfaceName = name
            , interfaceParent = scope
            , interfaceMethods          = [d | DescMethod    d <- members]
            , interfaceNestedAliases    = [d | DescAlias     d <- members]
            , interfaceNestedConstants  = [d | DescConstant  d <- members]
            , interfaceNestedEnums      = [d | DescEnum      d <- members]
            , interfaceNestedClasses    = [d | DescClass     d <- members]
            , interfaceNestedInterfaces = [d | DescInterface d <- members]
            , interfaceOptions = options
            , interfaceMembers = members
            , interfaceMemberMap = memberMap
            })))

compileDecl scope (MethodDecl (Located _ name) (Located _ number) params returnType decls) =
    CompiledMember name (feedback (\desc -> do
        paramDescs <- doAll (map (compileParam scope) params)
        returnTypeDesc <- compileType scope returnType
        (_, _, options) <- compileChildDecls desc decls
        return (DescMethod MethodDesc
            { methodName = name
            , methodParent = scope
            , methodNumber = number
            , methodParams = paramDescs
            , methodReturnType = returnTypeDesc
            , methodOptions = options
            })))

compileDecl scope (OptionDecl name (Located valuePos value)) =
    CompiledOption (do
        uncheckedOptionDesc <- lookupDesc scope name
        optionDesc <- case uncheckedOptionDesc of
            (DescOption d) -> return d
            _ -> makeError (declNamePos name) (printf "'%s' is not an option." (declNameString name))
        valueDesc <- compileValue valuePos (optionType optionDesc) value
        return OptionAssignmentDesc
            { optionAssignmentOption = optionDesc
            , optionAssignmentValue = valueDesc
            })

compileParam scope (name, typeExp, defaultValue) = do
    typeDesc <- compileType scope typeExp
    defaultDesc <- case defaultValue of
        Just (Located pos value) -> fmap Just (compileValue pos typeDesc value)
        Nothing -> return Nothing
    return (name, typeDesc, defaultDesc)

compileFile name decls =
    feedback (\desc -> do
        (members, memberMap, options) <- compileChildDecls (DescFile desc) decls
        return FileDesc
            { fileName = name
            , fileImports = []
            , fileAliases    = [d | DescAlias     d <- members]
            , fileConstants  = [d | DescConstant  d <- members]
            , fileEnums      = [d | DescEnum      d <- members]
            , fileClasses    = [d | DescClass     d <- members]
            , fileInterfaces = [d | DescInterface d <- members]
            , fileOptions = options
            , fileMembers = members
            , fileMemberMap = memberMap
            , fileImportMap = undefined
            })

parseAndCompileFile filename text = result where
    (decls, parseErrors) = parseFile filename text
    result = statusAddErrors parseErrors (compileFile filename decls)
