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
import Data.Map((!))
import qualified Data.List as List
import Data.Maybe(mapMaybe, fromMaybe)
import Text.Parsec.Pos(SourcePos, newPos)
import Text.Parsec.Error(ParseError, newErrorMessage, Message(Message, Expect))
import Text.Printf(printf)
import Util(delimit)

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

doAll statuses = Active [x | (Active x _) <- statuses] (concatMap statusErrors statuses)

------------------------------------------------------------------------------------------
-- Symbol lookup
------------------------------------------------------------------------------------------

-- | Look up a direct member of a descriptor by name.
descMember name (DescFile      d) = lookupMember name (fileMemberMap d)
descMember name (DescEnum      d) = lookupMember name (enumMemberMap d)
descMember name (DescStruct    d) = lookupMember name (structMemberMap d)
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

fromIntegerChecked :: Integral a => String -> SourcePos -> Integer -> Status a
fromIntegerChecked name pos x = result where
    unchecked = fromInteger x
    result = if toInteger unchecked == x
        then succeed unchecked
        else makeError pos (printf "Integer %d out of range for type %s." x name)

compileFieldAssignment :: StructDesc -> (Located String, Located FieldValue)
                       -> Status (FieldDesc, ValueDesc)
compileFieldAssignment desc (Located namePos name, Located valPos val) =
    case lookupMember name (structMemberMap desc) of
        Just (DescField field) ->
            fmap (\x -> (field, x)) (compileValue valPos (fieldType field) val)
        _ -> makeError namePos (printf "Struct %s has no field %s." (structName desc) name)

compileValue :: SourcePos -> TypeDesc -> FieldValue -> Status ValueDesc
compileValue _ (BuiltinType BuiltinVoid) VoidFieldValue = succeed VoidDesc
compileValue _ (BuiltinType BuiltinBool) (BoolFieldValue x) = succeed (BoolDesc x)
compileValue pos (BuiltinType BuiltinInt8  ) (IntegerFieldValue x) = fmap Int8Desc   (fromIntegerChecked "Int8"   pos x)
compileValue pos (BuiltinType BuiltinInt16 ) (IntegerFieldValue x) = fmap Int16Desc  (fromIntegerChecked "Int16"  pos x)
compileValue pos (BuiltinType BuiltinInt32 ) (IntegerFieldValue x) = fmap Int32Desc  (fromIntegerChecked "Int32"  pos x)
compileValue pos (BuiltinType BuiltinInt64 ) (IntegerFieldValue x) = fmap Int64Desc  (fromIntegerChecked "Int64"  pos x)
compileValue pos (BuiltinType BuiltinUInt8 ) (IntegerFieldValue x) = fmap UInt8Desc  (fromIntegerChecked "UInt8"  pos x)
compileValue pos (BuiltinType BuiltinUInt16) (IntegerFieldValue x) = fmap UInt16Desc (fromIntegerChecked "UInt16" pos x)
compileValue pos (BuiltinType BuiltinUInt32) (IntegerFieldValue x) = fmap UInt32Desc (fromIntegerChecked "UInt32" pos x)
compileValue pos (BuiltinType BuiltinUInt64) (IntegerFieldValue x) = fmap UInt64Desc (fromIntegerChecked "UInt64" pos x)
compileValue _ (BuiltinType BuiltinFloat32) (FloatFieldValue x) = succeed (Float32Desc (realToFrac x))
compileValue _ (BuiltinType BuiltinFloat64) (FloatFieldValue x) = succeed (Float64Desc x)
compileValue _ (BuiltinType BuiltinFloat32) (IntegerFieldValue x) = succeed (Float32Desc (realToFrac x))
compileValue _ (BuiltinType BuiltinFloat64) (IntegerFieldValue x) = succeed (Float64Desc (realToFrac x))
compileValue _ (BuiltinType BuiltinText) (StringFieldValue x) = succeed (TextDesc x)
compileValue _ (BuiltinType BuiltinData) (StringFieldValue x) =
    succeed (DataDesc (map (fromIntegral . fromEnum) x))

compileValue pos (EnumType desc) (IdentifierFieldValue name) =
    case lookupMember name (enumMemberMap desc) of
        Just (DescEnumValue value) -> succeed (EnumValueValueDesc value)
        _ -> makeError pos (printf "Enum type '%s' has no value '%s'." (enumName desc) name)

compileValue pos (StructType desc) (RecordFieldValue fields) = do
    assignments <- doAll (map (compileFieldAssignment desc) fields)

    -- Check for duplicate fields.
    _ <- let
        dupes = findDupesBy id [fieldName f | (f, _) <- assignments]
        errors = map dupFieldError dupes
        dupFieldError [] = error "empty group?"
        dupFieldError (name:_) = makeError pos
            (printf "Struct literal assigns field '%s' multiple times." name)
        in doAll errors

    -- Check for multiple assignments in the same union.
    _ <- let
        dupes = findDupesBy (\(_, u) -> unionName u)
            [(f, u) | (f@(FieldDesc {fieldUnion = Just u}), _) <- assignments]
        errors = map dupUnionError dupes
        dupUnionError [] = error "empty group?"
        dupUnionError dupFields@((_, u):_) = makeError pos (printf
            "Struct literal assigns multiple fields belonging to the same union '%s': %s"
            (unionName u) (delimit ", " (map (\(f, _) -> fieldName f) dupFields)))
        in doAll errors

    return (StructValueDesc assignments)

compileValue _ (ListType t) (ListFieldValue l) =
    fmap ListDesc (doAll [ compileValue vpos t v | Located vpos v <- l ])

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
compileValue pos (BuiltinType BuiltinData) _ = makeExpectError pos "string"

compileValue pos (EnumType _) _ = makeExpectError pos "enum value name"
compileValue pos (StructType _) _ = makeExpectError pos "parenthesized list of field assignments"
compileValue pos (InterfaceType _) _ = makeError pos "Interfaces can't have default values."
compileValue pos (ListType _) _ = makeExpectError pos "list"

makeFileMemberMap :: FileDesc -> Map.Map String Desc
makeFileMemberMap desc = Map.fromList allMembers where
    allMembers = [ (aliasName     m, DescAlias     m) | m <- fileAliases    desc ]
              ++ [ (constantName  m, DescConstant  m) | m <- fileConstants  desc ]
              ++ [ (enumName      m, DescEnum      m) | m <- fileEnums      desc ]
              ++ [ (structName    m, DescStruct    m) | m <- fileStructs    desc ]
              ++ [ (interfaceName m, DescInterface m) | m <- fileInterfaces desc ]

descAsType _ (DescEnum desc) = succeed (EnumType desc)
descAsType _ (DescStruct desc) = succeed (StructType desc)
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

------------------------------------------------------------------------------------------

findDupesBy :: Ord a => (b -> a) -> [b] -> [[b]]
findDupesBy getKey items = let
    compareItems a b = compare (getKey a) (getKey b)
    eqItems a b = getKey a == getKey b
    grouped = List.groupBy eqItems $ List.sortBy compareItems items
    in [ item | item@(_:_:_) <- grouped ]

requireSequentialNumbering :: String -> [Located Integer] -> Status ()
requireSequentialNumbering kind items = Active () (loop undefined (-1) sortedItems) where
    sortedItems = List.sort items
    loop _ _ [] = []
    loop _ prev (Located pos num:rest) | num == prev + 1 = loop pos num rest
    loop prevPos prev (Located pos num:rest) | num == prev = err1:err2:loop pos num rest where
        err1 = newErrorMessage (Message message) prevPos
        err2 = newErrorMessage (Message message) pos
        message = printf "Duplicate number %d.  %s must be numbered uniquely within their scope."
            num kind
    loop _ prev (Located pos num:rest) = err:loop pos num rest where
        err = newErrorMessage (Message message) pos
        message = printf "Skipped number %d.  %s must be numbered sequentially starting \
                         \from zero." (prev + 1) kind

requireOrdinalsInRange ordinals =
    Active () [ ordinalError num pos | Located pos num <- ordinals, num > maxOrdinal ] where
        ordinalError num = newErrorMessage (Message
            (printf "Ordinal %d too large; maximum is %d." num maxOrdinal))

requireNoDuplicateNames :: [Declaration] -> Status()
requireNoDuplicateNames decls = Active () (loop (List.sort locatedNames)) where
    locatedNames = mapMaybe declarationName decls
    loop (Located pos1 val1:Located pos2 val2:t) =
        if val1 == val2
            then dupError val1 pos1:dupError val2 pos2:loop2 val1 t
            else loop t
    loop _ = []
    loop2 val1 l@(Located pos2 val2:t) =
        if val1 == val2
            then dupError val2 pos2:loop2 val1 t
            else loop l
    loop2 _ _ = []

    dupError val = newErrorMessage (Message message) where
        message = printf "Duplicate declaration \"%s\"." val

fieldInUnion name f = case fieldUnion f of
    Nothing -> False
    Just x -> unionName x == name

requireNoMoreThanOneFieldNumberLessThan name pos num fields = Active () errors where
    retroFields = [fieldName f | f <- fields, fieldNumber f < num]
    message = printf "No more than one field in a union may have a number less than the \
                     \union's number, as it is not possible to retroactively unionize fields that \
                     \had been separate.  The following fields of union '%s' have lower numbers: %s"
                     name (delimit ", " retroFields)
    errors = if length retroFields <= 1
        then []
        else [newErrorMessage (Message message) pos]

------------------------------------------------------------------------------------------

initialPackingState = PackingState 0 0 0 0 0 0

packValue :: FieldSize -> PackingState -> (Integer, PackingState)
packValue Size64 s@(PackingState { packingDataSize = ds }) =
    (ds, s { packingDataSize = ds + 1 })
packValue SizeReference s@(PackingState { packingReferenceCount = rc }) =
    (rc, s { packingReferenceCount = rc + 1 })
packValue (SizeInlineComposite _ _) _ = error "Inline fields not yet supported."
packValue Size32 s@(PackingState { packingHole32 = 0 }) =
    case packValue Size64 s of
        (o64, s2) -> (o64 * 2, s2 { packingHole32 = o64 * 2 + 1 })
packValue Size32 s@(PackingState { packingHole32 = h32 }) =
    (h32, s { packingHole32 = 0 })
packValue Size16 s@(PackingState { packingHole16 = 0 }) =
    case packValue Size32 s of
        (o32, s2) -> (o32 * 2, s2 { packingHole16 = o32 * 2 + 1 })
packValue Size16 s@(PackingState { packingHole16 = h16 }) =
    (h16, s { packingHole16 = 0 })
packValue Size8 s@(PackingState { packingHole8 = 0 }) =
    case packValue Size16 s of
        (o16, s2) -> (o16 * 2, s2 { packingHole8 = o16 * 2 + 1 })
packValue Size8 s@(PackingState { packingHole8 = h8 }) =
    (h8, s { packingHole8 = 0 })
packValue Size1 s@(PackingState { packingHole1 = 0 }) =
    case packValue Size8 s of
        (o8, s2) -> (o8 * 8, s2 { packingHole1 = o8 * 8 + 1 })
packValue Size1 s@(PackingState { packingHole1 = h1 }) =
    (h1, s { packingHole1 = if mod (h1 + 1) 8 == 0 then 0 else h1 + 1 })
packValue Size0 s = (0, s)

initialUnionPackingState = UnionPackingState Nothing Nothing

packUnionizedValue :: FieldSize             -- Size of field to pack.
                   -> UnionPackingState     -- Current layout of the union
                   -> PackingState          -- Current layout of the struct.
                   -> (Integer, UnionPackingState, PackingState)
packUnionizedValue (SizeInlineComposite _ _) _ _ = error "Can't put inline composite into union."
packUnionizedValue Size0 u s = (0, u, s)

-- Pack reference when we already have a reference slot allocated.
packUnionizedValue SizeReference u@(UnionPackingState _ (Just offset)) s = (offset, u, s)

-- Pack reference when we don't have a reference slot.
packUnionizedValue SizeReference (UnionPackingState d Nothing) s = (offset, u2, s2) where
    (offset, s2) = packValue SizeReference s
    u2 = UnionPackingState d (Just offset)

-- Pack data.
packUnionizedValue size (UnionPackingState d r) s =
    case packUnionizedData (fromMaybe (0, Size0) d) s size of
        Just (offset, slotOffset, slotSize, s2) ->
            (offset, UnionPackingState (Just (slotOffset, slotSize)) r, s2)
        Nothing -> let
            (offset, s2) = packValue size s
            in (offset, UnionPackingState (Just (offset, size)) r, s2)

packUnionizedData :: (Integer, FieldSize)        -- existing slot to expand
                  -> PackingState                -- existing packing state
                  -> FieldSize                   -- desired field size
                  -> Maybe (Integer,       -- Offset of the new field (in multiples of field size).
                            Integer,       -- New offset of the slot (in multiples of slot size).
                            FieldSize,     -- New size of the slot.
                            PackingState)  -- New struct packing state.

-- Don't try to allocate space for voids.
packUnionizedData (slotOffset, slotSize) state Size0 = Just (0, slotOffset, slotSize, state)

-- If slot is bigger than desired size, no expansion is needed.
packUnionizedData (slotOffset, slotSize) state desiredSize
    | sizeInBits slotSize >= sizeInBits desiredSize =
    Just (div (sizeInBits slotSize) (sizeInBits desiredSize) * slotOffset,
          slotOffset, slotSize, state)

-- If slot is a bit, and it is the first bit in its byte, and the bit hole immediately follows
-- expand it to a byte.
packUnionizedData (slotOffset, Size1) p@(PackingState { packingHole1 = hole }) desiredSize
    | mod slotOffset 8 == 0 && hole == slotOffset + 1 =
        packUnionizedData (div slotOffset 8, Size8) (p { packingHole1 = 0 }) desiredSize

-- If slot is size N, and the next N bits are padding, expand.
packUnionizedData (slotOffset, Size8) p@(PackingState { packingHole8 = hole }) desiredSize
    | hole == slotOffset + 1 =
        packUnionizedData (div slotOffset 2, Size16) (p { packingHole8 = 0 }) desiredSize
packUnionizedData (slotOffset, Size16) p@(PackingState { packingHole16 = hole }) desiredSize
    | hole == slotOffset + 1 =
        packUnionizedData (div slotOffset 2, Size32) (p { packingHole16 = 0 }) desiredSize
packUnionizedData (slotOffset, Size32) p@(PackingState { packingHole32 = hole }) desiredSize
    | hole == slotOffset + 1 =
        packUnionizedData (div slotOffset 2, Size64) (p { packingHole32 = 0 }) desiredSize

-- Otherwise, we fail.
packUnionizedData _ _ _ = Nothing

-- Determine the offset for the given field, and update the packing states to include the field.
packField :: FieldDesc -> PackingState -> Map.Map Integer UnionPackingState
          -> (Integer, PackingState, Map.Map Integer UnionPackingState)
packField fieldDesc state unionState =
    case fieldUnion fieldDesc of
        Nothing -> let
            (offset, newState) = packValue (fieldSize $ fieldType fieldDesc) state
            in (offset, newState, unionState)
        Just unionDesc -> let
            n = unionNumber unionDesc
            oldUnionPacking = fromMaybe initialUnionPackingState (Map.lookup n unionState)
            (offset, newUnionPacking, newState) =
                packUnionizedValue (fieldSize $ fieldType fieldDesc) oldUnionPacking state
            newUnionState = Map.insert n newUnionPacking unionState
            in (offset, newState, newUnionState)

-- Determine the offset for the given union, and update the packing states to include the union.
-- Specifically, this packs the union tag, *not* the fields of the union.
packUnion :: UnionDesc -> PackingState -> Map.Map Integer UnionPackingState
          -> (Integer, PackingState, Map.Map Integer UnionPackingState)
packUnion _ state unionState = (offset, newState, unionState) where
    (offset, newState) = packValue Size16 state

packFields :: [FieldDesc] -> [UnionDesc]
    -> (PackingState, Map.Map Integer UnionPackingState, Map.Map Integer (Integer, PackingState))
packFields fields unions = (finalState, finalUnionState, Map.fromList packedItems) where
    items = [(fieldNumber d, packField d) | d <- fields] ++
            [(unionNumber d, packUnion d) | d <- unions]

    itemsByNumber = List.sortBy compareNumbers items
    compareNumbers (a, _) (b, _) = compare a b

    (finalState, finalUnionState, packedItems) =
        foldl packItem (initialPackingState, Map.empty, []) itemsByNumber

    packItem (state, unionState, packed) (n, item) =
        (newState, newUnionState, (n, (offset, newState)):packed) where
            (offset, newState, newUnionState) = item state unionState

------------------------------------------------------------------------------------------

-- For CompiledMemberStatus, the second parameter contains members that should be inserted into the
-- parent's map, e.g. fields defined in a union which should be considered members of the parent
-- struct as well.  Usually (except in the case of unions) this map is empty.
data CompiledStatementStatus = CompiledMemberStatus String (Status Desc)
                             | CompiledOptionStatus (Status OptionAssignmentDesc)

toCompiledStatement :: CompiledStatementStatus -> Maybe CompiledStatement
toCompiledStatement (CompiledMemberStatus _ (Active desc _)) = Just (CompiledMember desc)
toCompiledStatement (CompiledOptionStatus (Active desc _)) = Just (CompiledOption desc)
toCompiledStatement _ = Nothing

compiledErrors (CompiledMemberStatus _ status) = statusErrors status
compiledErrors (CompiledOptionStatus status) = statusErrors status

compileChildDecls :: Desc -> [Declaration]
                  -> Status ([Desc], MemberMap, OptionMap, [CompiledStatement])
compileChildDecls desc decls = Active (members, memberMap, options, statements) errors where
    compiledDecls = map (compileDecl desc) decls
    memberMap = Map.fromList memberPairs
    members = [member | (_, Just member) <- memberPairs]
    memberPairs = [(name, statusToMaybe status) | CompiledMemberStatus name status <- compiledDecls]
    options = Map.fromList [(optionName (optionAssignmentOption o), o)
                           | CompiledOptionStatus (Active o _) <- compiledDecls]
    errors = concatMap compiledErrors compiledDecls
    statements = mapMaybe toCompiledStatement compiledDecls

compileDecl scope (AliasDecl (Located _ name) target) =
    CompiledMemberStatus name (do
        targetDesc <- lookupDesc scope target
        return (DescAlias AliasDesc
            { aliasName = name
            , aliasParent = scope
            , aliasTarget = targetDesc
            }))

compileDecl scope (ConstantDecl (Located _ name) t (Located valuePos value)) =
    CompiledMemberStatus name (do
        typeDesc <- compileType scope t
        valueDesc <- compileValue valuePos typeDesc value
        return (DescConstant ConstantDesc
            { constantName = name
            , constantParent = scope
            , constantType = typeDesc
            , constantValue = valueDesc
            }))

compileDecl scope (EnumDecl (Located _ name) decls) =
    CompiledMemberStatus name (feedback (\desc -> do
        (members, memberMap, options, statements) <- compileChildDecls desc decls
        requireNoDuplicateNames decls
        let numbers = [ num | EnumValueDecl _ num _ <- decls ]
        requireSequentialNumbering "Enum values" numbers
        requireOrdinalsInRange numbers
        return (DescEnum EnumDesc
            { enumName = name
            , enumParent = scope
            , enumValues = [d | DescEnumValue d <- members]
            , enumOptions = options
            , enumMemberMap = memberMap
            , enumStatements = statements
            })))

compileDecl (DescEnum parent) (EnumValueDecl (Located _ name) (Located _ number) decls) =
    CompiledMemberStatus name (feedback (\desc -> do
        (_, _, options, statements) <- compileChildDecls desc decls
        return (DescEnumValue EnumValueDesc
            { enumValueName = name
            , enumValueParent = parent
            , enumValueNumber = number
            , enumValueOptions = options
            , enumValueStatements = statements
            })))
compileDecl _ (EnumValueDecl (Located pos name) _ _) =
    CompiledMemberStatus name (makeError pos "Enum values can only appear inside enums.")

compileDecl scope (StructDecl (Located _ name) decls) =
    CompiledMemberStatus name (feedback (\desc -> do
        (members, memberMap, options, statements) <- compileChildDecls desc decls
        requireNoDuplicateNames decls
        let fieldNums = [ num | FieldDecl _ num _ _ _ _ <- decls ] ++
                        [ num | UnionDecl _ num _ <- decls ]
        requireSequentialNumbering "Fields" fieldNums
        requireOrdinalsInRange fieldNums
        return (let
            fields = [d | DescField d <- members]
            unions = [d | DescUnion d <- members]
            (packing, _, fieldPackingMap) = packFields fields unions
            in DescStruct StructDesc
            { structName = name
            , structParent = scope
            , structPacking = packing
            , structFields = fields
            , structUnions = unions
            , structNestedAliases    = [d | DescAlias     d <- members]
            , structNestedConstants  = [d | DescConstant  d <- members]
            , structNestedEnums      = [d | DescEnum      d <- members]
            , structNestedStructs    = [d | DescStruct    d <- members]
            , structNestedInterfaces = [d | DescInterface d <- members]
            , structOptions = options
            , structMemberMap = memberMap
            , structStatements = statements
            , structFieldPackingMap = fieldPackingMap
            })))

compileDecl (DescStruct parent) (UnionDecl (Located _ name) (Located numPos number) decls) =
    CompiledMemberStatus name (feedback (\desc -> do
        (_, _, options, statements) <- compileChildDecls desc decls
        let compareFieldNumbers a b = compare (fieldNumber a) (fieldNumber b)
            fields = List.sortBy compareFieldNumbers
                [f | f <- structFields parent, fieldInUnion name f]
        requireNoMoreThanOneFieldNumberLessThan name numPos number fields
        return (let
            (tagOffset, tagPacking) = structFieldPackingMap parent ! number
            in DescUnion UnionDesc
            { unionName = name
            , unionParent = parent
            , unionNumber = number
            , unionTagOffset = tagOffset
            , unionTagPacking = tagPacking
            , unionFields = fields
            , unionOptions = options
            , unionStatements = statements
            })))
compileDecl _ (UnionDecl (Located pos name) _ _) =
    CompiledMemberStatus name (makeError pos "Unions can only appear inside structs.")

compileDecl scope@(DescStruct parent)
            (FieldDecl (Located _ name) (Located _ number) union typeExp defaultValue decls) =
    CompiledMemberStatus name (feedback (\desc -> do
        unionDesc <- case union of
            Nothing -> return Nothing
            Just (Located p n) -> do
                udesc <- maybeError (descMember n scope) p
                    (printf "No union '%s' defined in '%s'." n (structName parent))
                case udesc of
                    DescUnion d -> return (Just d)
                    _ -> makeError p (printf "'%s' is not a union." n)
        typeDesc <- compileType scope typeExp
        defaultDesc <- case defaultValue of
            Just (Located pos value) -> fmap Just (compileValue pos typeDesc value)
            Nothing -> return Nothing
        (_, _, options, statements) <- compileChildDecls desc decls
        return (let
            (offset, packing) = structFieldPackingMap parent ! number
            in DescField FieldDesc
            { fieldName = name
            , fieldParent = parent
            , fieldNumber = number
            , fieldOffset = offset
            , fieldPacking = packing
            , fieldUnion = unionDesc
            , fieldType = typeDesc
            , fieldDefaultValue = defaultDesc
            , fieldOptions = options
            , fieldStatements = statements
            })))
compileDecl _ (FieldDecl (Located pos name) _ _ _ _ _) =
    CompiledMemberStatus name (makeError pos "Fields can only appear inside structs.")

compileDecl scope (InterfaceDecl (Located _ name) decls) =
    CompiledMemberStatus name (feedback (\desc -> do
        (members, memberMap, options, statements) <- compileChildDecls desc decls
        requireNoDuplicateNames decls
        let numbers = [ num | MethodDecl _ num _ _ _ <- decls ]
        requireSequentialNumbering "Methods" numbers
        requireOrdinalsInRange numbers
        return (DescInterface InterfaceDesc
            { interfaceName = name
            , interfaceParent = scope
            , interfaceMethods          = [d | DescMethod    d <- members]
            , interfaceNestedAliases    = [d | DescAlias     d <- members]
            , interfaceNestedConstants  = [d | DescConstant  d <- members]
            , interfaceNestedEnums      = [d | DescEnum      d <- members]
            , interfaceNestedStructs    = [d | DescStruct    d <- members]
            , interfaceNestedInterfaces = [d | DescInterface d <- members]
            , interfaceOptions = options
            , interfaceMemberMap = memberMap
            , interfaceStatements = statements
            })))

compileDecl scope@(DescInterface parent)
            (MethodDecl (Located _ name) (Located _ number) params returnType decls) =
    CompiledMemberStatus name (feedback (\desc -> do
        paramDescs <- doAll (map (compileParam scope) params)
        returnTypeDesc <- compileType scope returnType
        (_, _, options, statements) <- compileChildDecls desc decls
        return (DescMethod MethodDesc
            { methodName = name
            , methodParent = parent
            , methodNumber = number
            , methodParams = paramDescs
            , methodReturnType = returnTypeDesc
            , methodOptions = options
            , methodStatements = statements
            })))
compileDecl _ (MethodDecl (Located pos name) _ _ _ _) =
    CompiledMemberStatus name (makeError pos "Methods can only appear inside interfaces.")

compileDecl scope (OptionDecl name (Located valuePos value)) =
    CompiledOptionStatus (do
        uncheckedOptionDesc <- lookupDesc scope name
        optionDesc <- case uncheckedOptionDesc of
            (DescOption d) -> return d
            _ -> makeError (declNamePos name) (printf "'%s' is not an option." (declNameString name))
        valueDesc <- compileValue valuePos (optionType optionDesc) value
        return OptionAssignmentDesc
            { optionAssignmentParent = scope
            , optionAssignmentOption = optionDesc
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
        (members, memberMap, options, statements) <- compileChildDecls (DescFile desc) decls
        requireNoDuplicateNames decls
        return FileDesc
            { fileName = name
            , fileImports = []
            , fileAliases    = [d | DescAlias     d <- members]
            , fileConstants  = [d | DescConstant  d <- members]
            , fileEnums      = [d | DescEnum      d <- members]
            , fileStructs    = [d | DescStruct    d <- members]
            , fileInterfaces = [d | DescInterface d <- members]
            , fileOptions = options
            , fileMemberMap = memberMap
            , fileImportMap = undefined
            , fileStatements = statements
            })

parseAndCompileFile filename text = result where
    (decls, parseErrors) = parseFile filename text
    -- Here we're doing the copmile step even if there were errors in parsing, and just combining
    -- all the errors together.  This may allow the user to fix more errors per compiler iteration,
    -- but it might also be confusing if a parse error causes a subsequent compile error, especially
    -- if the compile error ends up being on a line before the parse error (e.g. there's a parse
    -- error in a type definition, causing a not-defined error on a field trying to use that type).
    -- TODO:  Re-evaluate after getting some experience on whether this is annoing.
    result = statusAddErrors parseErrors (compileFile filename decls)
