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

module Compiler (Status(..), parseAndCompileFile) where

import Grammar
import Semantics
import Token(Located(Located), locatedValue)
import Parser(parseFile)
import Control.Monad(when, unless, liftM)
import qualified Data.Map as Map
import Data.Map((!))
import qualified Data.Set as Set
import qualified Data.List as List
import Data.Maybe(mapMaybe, fromMaybe, isJust, isNothing)
import Data.Word(Word64, Word8)
import Text.Parsec.Pos(SourcePos, newPos)
import Text.Parsec.Error(ParseError, newErrorMessage, Message(Message, Expect))
import Text.Printf(printf)
import qualified Data.Digest.MD5 as MD5
import qualified Codec.Binary.UTF8.String as UTF8
import Util(delimit, intToBytes)
import Data.Bits(setBit)

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

-- Recovers from Failed status by using a fallback result, but keeps the errors.
--
-- This function is carefully written such that the runtime can see that it returns Active without
-- actually evaluating the parameters.  The parameters are only evaluated when the returned value
-- or errors are examined.
recover :: a -> Status a -> Status a
recover fallback status = Active value errs where
    (value, errs) = case status of
        Active v e -> (v, e)
        Failed e -> (fallback, e)

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
descMember name (DescUsing     d) = descMember name (usingTarget d)
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
     [ ("List", DescBuiltinList)
{- Inlines have been disabled for now because they added too much complication.
     , ("Inline", DescBuiltinInline)
     , ("InlineList", DescBuiltinInlineList)
     , ("InlineData", DescBuiltinInlineData)
-}
     ])

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
        Just (DescUnion union) -> case val of
            UnionFieldValue uName uVal ->
                case lookupMember uName (unionMemberMap union) of
                    Just (DescField field) ->
                        fmap (\x -> (field, x)) (compileValue valPos (fieldType field) uVal)
                    _ -> makeError namePos (printf "Union %s has no member %s."
                        (unionName union) uName)
            _ -> makeExpectError valPos "union value"
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
compileValue _ (BuiltinType BuiltinFloat32) (IdentifierFieldValue "inf") = succeed $ Float32Desc $ 1.0 / 0.0
compileValue _ (BuiltinType BuiltinFloat64) (IdentifierFieldValue "inf") = succeed $ Float64Desc $ 1.0 / 0.0
compileValue _ (BuiltinType BuiltinFloat32) (IdentifierFieldValue "nan") = succeed $ Float32Desc $ 0.0 / 0.0
compileValue _ (BuiltinType BuiltinFloat64) (IdentifierFieldValue "nan") = succeed $ Float64Desc $ 0.0 / 0.0
compileValue _ (BuiltinType BuiltinText) (StringFieldValue x) = succeed (TextDesc x)
compileValue _ (BuiltinType BuiltinData) (StringFieldValue x) =
    succeed (DataDesc (map (fromIntegral . fromEnum) x))

compileValue pos (EnumType desc) (IdentifierFieldValue name) =
    case lookupMember name (enumMemberMap desc) of
        Just (DescEnumerant value) -> succeed (EnumerantValueDesc value)
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
            [(f, u) | (f@(FieldDesc {fieldUnion = Just (u, _)}), _) <- assignments]
        errors = map dupUnionError dupes
        dupUnionError [] = error "empty group?"
        dupUnionError dupFields@((_, u):_) = makeError pos (printf
            "Struct literal assigns multiple fields belonging to the same union '%s': %s"
            (unionName u) (delimit ", " (map (\(f, _) -> fieldName f) dupFields)))
        in doAll errors

    return (StructValueDesc assignments)

compileValue pos (InlineStructType desc) v = compileValue pos (StructType desc) v

compileValue _ (ListType t) (ListFieldValue l) =
    fmap ListDesc (doAll [ compileValue vpos t v | Located vpos v <- l ])

compileValue pos (InlineListType t s) (ListFieldValue l) = do
    elements <- doAll [ compileValue vpos t v | Located vpos v <- l ]
    when (List.genericLength elements /= s) $
        makeError pos $ printf "Fixed-size list must have exactly %d elements." s
    return $ ListDesc elements

compileValue pos (InlineDataType s) (StringFieldValue x) = let
    bytes = map (fromIntegral . fromEnum) x
    in if List.genericLength bytes == s
        then succeed $ DataDesc bytes
        else makeError pos $ printf "Fixed-size data must have exactly %d bytes." s

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
compileValue pos (BuiltinType BuiltinObject) _ =
    -- TODO(someday):  We could arguably design a syntax where you specify the type followed by
    --   the value, but it seems not worth the effort.
    makeError pos "Can't specify literal value for 'Object'."

compileValue pos (EnumType _) _ = makeExpectError pos "enumerant name"
compileValue pos (StructType _) _ = makeExpectError pos "parenthesized list of field assignments"
compileValue pos (InterfaceType _) _ = makeError pos "Can't specify literal value for interface."
compileValue pos (ListType _) _ = makeExpectError pos "list"
compileValue pos (InlineListType _ _) _ = makeExpectError pos "list"
compileValue pos (InlineDataType _) _ = makeExpectError pos "string"

descAsType _ (DescEnum desc) = succeed (EnumType desc)
descAsType _ (DescStruct desc) = succeed (StructType desc)
descAsType _ (DescInterface desc) = succeed (InterfaceType desc)
descAsType _ (DescBuiltinType desc) = succeed (BuiltinType desc)
descAsType name (DescUsing desc) = descAsType name (usingTarget desc)
descAsType name DescBuiltinList = makeError (declNamePos name) message where
            message = printf "'List' requires exactly one type parameter." (declNameString name)
descAsType name DescBuiltinInline = makeError (declNamePos name) message where
            message = printf "'Inline' requires exactly one type parameter." (declNameString name)
descAsType name DescBuiltinInlineList = makeError (declNamePos name) message where
            message = printf "'InlineList' requires exactly two type parameters." (declNameString name)
descAsType name DescBuiltinInlineData = makeError (declNamePos name) message where
            message = printf "'InlineData' requires exactly one type parameter." (declNameString name)
descAsType name _ = makeError (declNamePos name) message where
            message = printf "'%s' is not a type." (declNameString name)

compileType :: Desc -> TypeExpression -> Status TypeDesc
compileType scope (TypeExpression n params) = do
    desc <- lookupDesc scope n
    case desc of
        DescBuiltinList -> case params of
            [TypeParameterType param] ->  do
                inner <- compileType scope param
                case inner of
                    InlineStructType _ -> makeError (declNamePos n)
                        "Don't declare list elements 'Inline'.  The regular encoding for struct \
                        \lists already inlines the elements."
                    InlineListType (BuiltinType BuiltinBool) _ -> makeError (declNamePos n)
                        "List(InlineList(Bool, n)) not supported due to implementation difficulty."
                    BuiltinType BuiltinObject -> makeError (declNamePos n)
                        "List(Object) not supported.  Just use Object, or create a struct with \
                        \one field of type 'Object' and use a List of that."
                    _ -> return (ListType inner)
            _ -> makeError (declNamePos n) "'List' requires exactly one type parameter."
        DescBuiltinInline -> case params of
            [TypeParameterType param] -> do
                inner <- compileType scope param
                case inner of
                    StructType s -> if structIsFixedWidth s
                        then return (InlineStructType s)
                        else makeError (declNamePos n) $
                            printf "'%s' cannot be inlined because it is not fixed-width."
                                   (structName s)
                    _ -> makeError (declNamePos n) "'Inline' parameter must be a struct type."
            _ -> makeError (declNamePos n) "'Inline' requires exactly one type parameter."
        DescBuiltinInlineList -> case params of
            [TypeParameterType param, TypeParameterInteger size] -> do
                inner <- compileType scope param
                case inner of
                    InlineStructType _ -> makeError (declNamePos n)
                        "Don't declare list elements 'Inline'.  The regular encoding for struct \
                        \lists already inlines the elements."
                    StructType s -> if structIsFixedWidth s
                        then return (InlineListType (InlineStructType s) size)
                        else makeError (declNamePos n) $
                            printf "'%s' cannot be inlined because it is not fixed-width."
                                   (structName s)
                    InlineListType _ _ -> makeError (declNamePos n)
                        "InlineList of InlineList not currently supported."
                    InlineDataType _ -> makeError (declNamePos n)
                        "InlineList of InlineData not currently supported."
                    BuiltinType BuiltinObject -> makeError (declNamePos n)
                        "InlineList(Object) not supported."
                    _ -> return $ InlineListType inner size
            _ -> makeError (declNamePos n)
                "'InlineList' requires exactly two type parameters: a type and a size."
        DescBuiltinInlineData -> case params of
            [TypeParameterInteger size] -> return $ InlineDataType size
            _ -> makeError (declNamePos n)
                "'InlineData' requires exactly one type parameter: the byte size of the data."
        _ -> case params of
            [] -> descAsType n desc
            _ -> makeError (declNamePos n) $
                printf "'%s' doesn't take parameters." (declNameString n)

compileAnnotation :: Desc -> AnnotationTarget -> Annotation
                  -> Status (AnnotationDesc, ValueDesc)
compileAnnotation scope kind (Annotation name (Located pos value)) = do
    nameDesc <- lookupDesc scope name
    case nameDesc of
        DescAnnotation annDesc -> do
            unless (Set.member kind (annotationTargets annDesc))
                (makeError (declNamePos name)
                $ printf "'%s' cannot be used on %s." (declNameString name) (show kind))
            compiledValue <- compileValue pos (annotationType annDesc) value
            return (annDesc, compiledValue)
        _ -> makeError (declNamePos name)
           $ printf "'%s' is not an annotation." (declNameString name)

compileAnnotations :: Desc -> AnnotationTarget -> [Annotation]
                   -> Status AnnotationMap
compileAnnotations scope kind annotations = do
    let compileLocated ann@(Annotation name _) =
            fmap (Located $ declNamePos name) $ compileAnnotation scope kind ann

    compiled <- doAll $ map compileLocated annotations

    -- Makes a map entry for the annotation keyed by ID.
    let locatedEntries = [ Located pos (annotationId desc, (desc, v))
                         | Located pos (desc, v) <- compiled ]

        -- TODO(cleanup):  Generalize duplicate detection.
        sortedLocatedEntries = detectDup $ List.sortBy compareIds locatedEntries
        compareIds (Located _ (a, _)) (Located _ (b, _)) = compare a b
        detectDup (Located _ x@(id1, _):Located pos (id2, _):rest)
            | id1 == id2 = succeed x:makeError pos "Duplicate annotation.":detectDup rest
        detectDup (Located _ x:rest) = succeed x:detectDup rest
        detectDup [] = []

    finalEntries <- doAll sortedLocatedEntries

    return $ Map.fromList finalEntries

childId :: String -> Maybe (Located Word64) -> Desc -> Word64
childId _ (Just (Located _ myId)) _ = myId
childId name Nothing parent = let
    hash = MD5.hash (intToBytes (descId parent) 8 ++ UTF8.encode name)
    addByte :: Word64 -> Word8 -> Word64
    addByte b v = b * 256 + fromIntegral v
    in flip setBit 63 $ foldl addByte 0 (take 8 hash)

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

requireNoMoreThanOneFieldNumberLessThan name pos num fields = Active () errors where
    retroFields = [fieldName f | f <- fields, fieldNumber f < num]
    message = printf "No more than one field in a union may have a number less than the \
                     \union's number, as it is not possible to retroactively unionize fields that \
                     \had been separate.  The following fields of union '%s' have lower numbers: %s"
                     name (delimit ", " retroFields)
    errors = if length retroFields <= 1
        then []
        else [newErrorMessage (Message message) pos]

extractFieldNumbers :: [Declaration] -> [Located Integer]
extractFieldNumbers decls = concat
    ([ num | FieldDecl _ num _ _ _ <- decls ]
    :[ num:extractFieldNumbers uDecls | UnionDecl _ num _ uDecls <- decls ])

------------------------------------------------------------------------------------------

data PackingState = PackingState
    { packingHoles :: Map.Map DataSize Integer
    , packingDataSize :: Integer
    , packingPointerCount :: Integer
    }

initialPackingState = PackingState Map.empty 0 0

packValue :: FieldSize -> PackingState -> (FieldOffset, PackingState)
packValue SizeVoid s = (VoidOffset, s)
packValue SizePointer s@(PackingState { packingPointerCount = rc }) =
    (PointerOffset rc, s { packingPointerCount = rc + 1 })
packValue (SizeInlineComposite (DataSectionWords inlineDs) inlineRc)
          s@(PackingState { packingDataSize = ds, packingPointerCount = rc }) =
    (InlineCompositeOffset ds rc (DataSectionWords inlineDs) inlineRc,
        s { packingDataSize = ds + inlineDs
          , packingPointerCount = rc + inlineRc })
packValue (SizeInlineComposite inlineDs inlineRc)
          s@(PackingState { packingPointerCount = rc }) = let
    size = (dataSectionAlignment inlineDs)
    (offset, s2) = packData size s
    in (InlineCompositeOffset offset rc inlineDs inlineRc,
        s2 { packingPointerCount = rc + inlineRc })
packValue (SizeData size) s = let (o, s2) = packData size s in (DataOffset size o, s2)

packData :: DataSize -> PackingState -> (Integer, PackingState)
packData Size64 s@(PackingState { packingDataSize = ds }) =
    (ds, s { packingDataSize = ds + 1 })

packData size s = let
    -- updateLookupWithKey doesn't quite work here because it returns the new value if updated, or
    -- the old value if not.  We really always want the old value and have no way to distinguish.
    -- There appears to be no function that does this, AFAICT.
    hole = Map.lookup size $ packingHoles s
    newHoles = Map.update splitHole size $ packingHoles s
    splitHole off = case size of
        Size1 -> if mod off 8 == 7 then Nothing else Just (off + 1)
        _ -> Nothing
    in case hole of
        -- If there was a hole of the correct size, use it.
        Just off -> (off, s { packingHoles = newHoles })

        -- Otherwise, try to pack a value of the next size up, and then split it.
        Nothing -> let
            nextSize = succ size
            (nextOff, s2) = packData nextSize s
            off = demoteOffset nextSize nextOff
            newHoles2 = Map.insert size (off + 1) $ packingHoles s2
            in (off, s2 { packingHoles = newHoles2 })

-- Convert an offset of one data size to an offset of the next smaller size.
demoteOffset :: DataSize -> Integer -> Integer
demoteOffset Size1 _ = error "can't split bit"
demoteOffset Size8 i = i * 8
demoteOffset _ i = i * 2

data UnionSlot sizeType = UnionSlot sizeType Integer   -- size, offset
data UnionPackingState = UnionPackingState
    { unionDataSlot :: UnionSlot DataSectionSize
    , unionPointerSlot :: UnionSlot Integer }

initialUnionPackingState = UnionPackingState (UnionSlot (DataSectionWords 0) 0) (UnionSlot 0 0)

packUnionizedValue :: FieldSize             -- Size of field to pack.
                   -> UnionPackingState     -- Current layout of the union
                   -> PackingState          -- Current layout of the struct.
                   -> (FieldOffset, UnionPackingState, PackingState)

packUnionizedValue SizeVoid u s = (VoidOffset, u, s)

-- Pack data when there is no existing slot.
packUnionizedValue (SizeData size) (UnionPackingState (UnionSlot (DataSectionWords 0) _) p) s =
    let (offset, s2) = packData size s
    in (DataOffset size offset,
        UnionPackingState (UnionSlot (dataSizeToSectionSize size) offset) p, s2)

-- Pack data when there is a word-sized slot.  All data fits in a word.
packUnionizedValue (SizeData size)
                   ups@(UnionPackingState (UnionSlot (DataSectionWords _) offset) _) s =
    (DataOffset size (offset * div 64 (dataSizeInBits size)), ups, s)

-- Pack data when there is a non-word-sized slot.
packUnionizedValue (SizeData size) (UnionPackingState (UnionSlot slotSize slotOffset) p) s =
    case tryExpandSubWordDataSlot (dataSectionAlignment slotSize, slotOffset) s size of
        Just (offset, (newSlotSize, newSlotOffset), s2) ->
            (DataOffset size offset,
             UnionPackingState (UnionSlot (dataSizeToSectionSize newSlotSize) newSlotOffset) p, s2)
        -- If the slot wasn't big enough, pack as if there were no slot.
        Nothing -> packUnionizedValue (SizeData size)
            (UnionPackingState (UnionSlot (DataSectionWords 0) 0) p) s

-- Pack pointer when we don't have a pointer slot.
packUnionizedValue SizePointer u@(UnionPackingState _ (UnionSlot 0 _)) s = let
    (PointerOffset offset, s2) = packValue SizePointer s
    u2 = u { unionPointerSlot = UnionSlot 1 offset }
    in (PointerOffset offset, u2, s2)

-- Pack pointer when we already have a pointer slot allocated.
packUnionizedValue SizePointer u@(UnionPackingState _ (UnionSlot _ offset)) s =
    (PointerOffset offset, u, s)

-- Pack inline composite.
packUnionizedValue (SizeInlineComposite dataSize pointerCount)
        u@(UnionPackingState { unionDataSlot = UnionSlot dataSlotSize dataSlotOffset
                             , unionPointerSlot = UnionSlot pointerSlotSize pointerSlotOffset })
        s = let

    -- Pack the data section.
    (dataOffset, u2, s2) = case dataSize of
        DataSectionWords 0 -> (0, u, s)
        DataSectionWords requestedWordSize -> let
            maybeExpanded = case dataSlotSize of
                -- Try to expand existing n-word slot to fit.
                DataSectionWords existingWordSize ->
                    tryExpandUnionizedDataWords u s
                        dataSlotOffset existingWordSize requestedWordSize

                -- Try to expand the existing sub-word slot into a word, then from there to a slot
                -- of the size we need.
                _ -> do
                    (expandedSlotOffset, _, expandedPackingState) <-
                        tryExpandSubWordDataSlot (dataSectionAlignment dataSlotSize, dataSlotOffset)
                                                 s Size64
                    let newU = u { unionDataSlot =
                        UnionSlot (DataSectionWords 1) expandedSlotOffset }
                    tryExpandUnionizedDataWords newU expandedPackingState
                        expandedSlotOffset 1 requestedWordSize

            -- If expanding fails, fall back to appending the new words to the end of the struct.
            atEnd = (packingDataSize s,
                u { unionDataSlot = UnionSlot (DataSectionWords requestedWordSize)
                                              (packingDataSize s) },
                s { packingDataSize = packingDataSize s + requestedWordSize })

            in fromMaybe atEnd maybeExpanded
        _ -> let
            (DataOffset _ result, newU, newS) =
                packUnionizedValue (SizeData (dataSectionAlignment dataSize)) u s
            in (result, newU, newS)

    -- Pack the pointer section.
    (pointerOffset, u3, s3)
        | pointerCount <= pointerSlotSize = (pointerSlotOffset, u2, s2)
        | pointerSlotOffset + pointerSlotSize == packingPointerCount s2 =
            (pointerSlotOffset,
            u2 { unionPointerSlot = UnionSlot pointerCount pointerSlotOffset },
            s2 { packingPointerCount = pointerSlotOffset + pointerCount })
        | otherwise =
            (packingPointerCount s2,
            u2 { unionPointerSlot = UnionSlot pointerCount (packingPointerCount s2) },
            s2 { packingPointerCount = packingPointerCount s2 + pointerCount })

    combinedOffset = InlineCompositeOffset
        { inlineCompositeDataOffset = dataOffset
        , inlineCompositePointerOffset = pointerOffset
        , inlineCompositeDataSize = dataSize
        , inlineCompositePointerSize = pointerCount
        }

    in (combinedOffset, u3, s3)

tryExpandUnionizedDataWords unionState packingState existingOffset existingSize requestedSize
    -- Is the existing multi-word slot big enough?
    | requestedSize <= existingSize =
        -- Yes, use it.
        Just (existingOffset, unionState, packingState)
    -- Is the slot at the end of the struct?
    | existingOffset + existingSize == packingDataSize packingState =
        -- Yes, expand it.
        Just (existingOffset,
            unionState { unionDataSlot = UnionSlot (DataSectionWords requestedSize)
                                                   existingOffset },
            packingState { packingDataSize = packingDataSize packingState
                                           + requestedSize - existingSize })
    | otherwise = Nothing

-- Try to expand an existing data slot to be big enough for a data field.
tryExpandSubWordDataSlot :: (DataSize, Integer)          -- existing slot to expand
                         -> PackingState                 -- existing packing state
                         -> DataSize                     -- desired field size
                         -> Maybe (Integer,              -- Offset of the new field.
                                   (DataSize, Integer),  -- New offset of the slot.
                                   PackingState)         -- New struct packing state.

-- If slot is bigger than desired size, no expansion is needed.
tryExpandSubWordDataSlot (slotSize, slotOffset) state desiredSize
    | dataSizeInBits slotSize >= dataSizeInBits desiredSize =
    Just (div (dataSizeInBits slotSize) (dataSizeInBits desiredSize) * slotOffset,
          (slotSize, slotOffset), state)

-- Try expanding the slot by combining it with subsequent padding.
tryExpandSubWordDataSlot (slotSize, slotOffset) state desiredSize = let
    nextSize = succ slotSize
    ratio = div (dataSizeInBits nextSize) (dataSizeInBits slotSize)
    isAligned = mod slotOffset ratio == 0
    nextOffset = div slotOffset ratio

    deleteHole _ _ = Nothing
    (maybeHole, newHoles) = Map.updateLookupWithKey deleteHole slotSize $ packingHoles state
    newState = state { packingHoles = newHoles }

    in if not isAligned
        then Nothing   -- Existing slot is not aligned properly.
        else case maybeHole of
            Just holeOffset | holeOffset == slotOffset + 1 ->
                tryExpandSubWordDataSlot (nextSize, nextOffset) newState desiredSize
            _ -> Nothing

-- Determine the offset for the given field, and update the packing states to include the field.
packField :: FieldDesc -> PackingState -> Map.Map Integer UnionPackingState
          -> (FieldOffset, PackingState, Map.Map Integer UnionPackingState)
packField fieldDesc state unionState =
    case fieldUnion fieldDesc of
        Nothing -> let
            (offset, newState) = packValue (fieldSize $ fieldType fieldDesc) state
            in (offset, newState, unionState)
        Just (unionDesc, _) -> let
            n = unionNumber unionDesc
            oldUnionPacking = fromMaybe initialUnionPackingState (Map.lookup n unionState)
            (offset, newUnionPacking, newState) =
                packUnionizedValue (fieldSize $ fieldType fieldDesc) oldUnionPacking state
            newUnionState = Map.insert n newUnionPacking unionState
            in (offset, newState, newUnionState)

-- Determine the offset for the given union, and update the packing states to include the union.
-- Specifically, this packs the union tag, *not* the fields of the union.
packUnion :: UnionDesc -> PackingState -> Map.Map Integer UnionPackingState
          -> (FieldOffset, PackingState, Map.Map Integer UnionPackingState)
packUnion _ state unionState = (DataOffset Size16 offset, newState, unionState) where
    (offset, newState) = packData Size16 state

stripHolesFromFirstWord Size1 _ = Size1  -- Stop at a bit.
stripHolesFromFirstWord size holes = let
    nextSize = pred size
    in case Map.lookup nextSize holes of
        Just 1 -> stripHolesFromFirstWord nextSize holes
        _ -> size

packFields :: [FieldDesc] -> [UnionDesc] -> (DataSectionSize, Integer, Map.Map Integer FieldOffset)
packFields fields unions = let
    items = concat (
        [(fieldNumber d, packField d) | d <- fields]:
        [(unionNumber d, packUnion d):[(fieldNumber d2, packField d2) | d2 <- unionFields d]
        | d <- unions])

    itemsByNumber = List.sortBy compareNumbers items
    compareNumbers (a, _) (b, _) = compare a b

    (finalState, _, packedItems) =
        foldl packItem (initialPackingState, Map.empty, []) itemsByNumber

    packItem (state, unionState, packed) (n, item) =
        (newState, newUnionState, (n, offset):packed) where
            (offset, newState, newUnionState) = item state unionState

    dataSectionSize =
        if packingDataSize finalState == 1
            then dataSizeToSectionSize $ stripHolesFromFirstWord Size64 $ packingHoles finalState
            else DataSectionWords $ packingDataSize finalState

    in (dataSectionSize, packingPointerCount finalState, Map.fromList packedItems)

enforceFixed Nothing sizes = return sizes
enforceFixed (Just (Located pos (requestedDataSize, requestedPointerCount)))
        (actualDataSize, actualPointerCount) = do
    validatedRequestedDataSize <- case requestedDataSize of
        1 -> return DataSection1
        8 -> return DataSection8
        16 -> return DataSection16
        32 -> return DataSection32
        s | mod s 64 == 0 -> return $ DataSectionWords $ div s 64
        _ -> makeError pos $ printf "Struct data section size must be 0, 1, 2, 4, or a multiple of \
                                    \8 bytes."

    recover () $ when (dataSectionBits actualDataSize > dataSectionBits validatedRequestedDataSize) $
        makeError pos $ printf "Struct data section size is %s which exceeds specified maximum of \
            \%s.  WARNING:  Increasing the maximum will break backwards-compatibility."
            (dataSectionSizeString actualDataSize)
            (dataSectionSizeString validatedRequestedDataSize)
    recover () $ when (actualPointerCount > requestedPointerCount) $
        makeError pos $ printf "Struct pointer section size is %d pointers which exceeds specified \
            \maximum of %d pointers.  WARNING:  Increasing the maximum will break \
            \backwards-compatibility."
            actualPointerCount requestedPointerCount

    recover () $ when (dataSectionBits actualDataSize > maxStructDataWords * 64) $
        makeError pos $ printf "Struct is too big.  Maximum data section size is %d bytes."
            (maxStructDataWords * 8)
    recover () $ when (actualPointerCount > maxStructPointers) $
        makeError pos $ printf "Struct is too big.  Maximum pointer section size is %d."
            maxStructPointers

    return (validatedRequestedDataSize, requestedPointerCount)

------------------------------------------------------------------------------------------

data CompiledStatementStatus = CompiledStatementStatus String (Status Desc)

compiledErrors (CompiledStatementStatus _ status) = statusErrors status

compileChildDecls :: Desc -> [Declaration]
                  -> Status ([Desc], MemberMap)
compileChildDecls desc decls = Active (members, memberMap) errors where
    compiledDecls = map (compileDecl desc) decls
    memberMap = Map.fromList memberPairs
    members = [member | (_, Just member) <- memberPairs]
    memberPairs = [(name, statusToMaybe status)
                  | CompiledStatementStatus name status <- compiledDecls]
    errors = concatMap compiledErrors compiledDecls

compileDecl scope (UsingDecl (Located _ name) target) =
    CompiledStatementStatus name (do
        targetDesc <- lookupDesc scope target
        return (DescUsing UsingDesc
            { usingName = name
            , usingParent = scope
            , usingTarget = targetDesc
            }))

compileDecl scope (ConstantDecl (Located _ name) t annotations (Located valuePos value)) =
    CompiledStatementStatus name (do
        typeDesc <- compileType scope t
        valueDesc <- compileValue valuePos typeDesc value
        compiledAnnotations <- compileAnnotations scope ConstantAnnotation annotations
        return (DescConstant ConstantDesc
            { constantName = name
            , constantId = childId name Nothing scope
            , constantParent = scope
            , constantType = typeDesc
            , constantValue = valueDesc
            , constantAnnotations = compiledAnnotations
            }))

compileDecl scope (EnumDecl (Located _ name) maybeTypeId annotations decls) =
    CompiledStatementStatus name (feedback (\desc -> do
        (members, memberMap) <- compileChildDecls desc decls
        requireNoDuplicateNames decls
        let numbers = [ num | EnumerantDecl _ num _ <- decls ]
        requireSequentialNumbering "Enumerants" numbers
        requireOrdinalsInRange numbers
        compiledAnnotations <- compileAnnotations scope EnumAnnotation annotations
        return (DescEnum EnumDesc
            { enumName = name
            , enumId = childId name maybeTypeId scope
            , enumParent = scope
            , enumerants = [d | DescEnumerant d <- members]
            , enumAnnotations = compiledAnnotations
            , enumMemberMap = memberMap
            , enumMembers = members
            })))

compileDecl scope@(DescEnum parent)
            (EnumerantDecl (Located _ name) (Located _ number) annotations) =
    CompiledStatementStatus name (do
        compiledAnnotations <- compileAnnotations scope EnumerantAnnotation annotations
        return (DescEnumerant EnumerantDesc
            { enumerantName = name
            , enumerantParent = parent
            , enumerantNumber = number
            , enumerantAnnotations = compiledAnnotations
            }))
compileDecl _ (EnumerantDecl (Located pos name) _ _) =
    CompiledStatementStatus name (makeError pos "Enumerants can only appear inside enums.")

compileDecl scope (StructDecl (Located _ name) maybeTypeId isFixed annotations decls) =
    CompiledStatementStatus name (feedback (\desc -> do
        (members, memberMap) <- compileChildDecls desc decls
        requireNoDuplicateNames decls
        let fieldNums = extractFieldNumbers decls
        requireSequentialNumbering "Fields" fieldNums
        requireOrdinalsInRange fieldNums
        compiledAnnotations <- compileAnnotations scope StructAnnotation annotations
        let (dataSize, pointerCount, fieldPackingMap) = packFields fields unions
            fields = [d | DescField d <- members]
            unions = [d | DescUnion d <- members]
        (finalDataSize, finalPointerCount) <-
            recover (dataSize, pointerCount) $ enforceFixed isFixed (dataSize, pointerCount)
        return (let
            in DescStruct StructDesc
            { structName = name
            , structId = childId name maybeTypeId scope
            , structParent = scope
            , structDataSize = finalDataSize
            , structPointerCount = finalPointerCount
            , structIsFixedWidth = isJust isFixed
            , structFields = fields
            , structUnions = unions
            , structAnnotations = compiledAnnotations
            , structMemberMap = memberMap
            , structMembers = members
            , structFieldPackingMap = fieldPackingMap
            })))

compileDecl scope@(DescStruct parent)
            (UnionDecl (Located _ name) (Located numPos number) annotations decls) =
    CompiledStatementStatus name (feedback (\desc -> do
        (members, memberMap) <- compileChildDecls desc decls
        let fields = [f | DescField f <- members]
            orderedFieldNumbers = List.sort $ map fieldNumber fields
            discriminantMap = Map.fromList $ zip orderedFieldNumbers [0..]
        requireNoMoreThanOneFieldNumberLessThan name numPos number fields
        compiledAnnotations <- compileAnnotations scope UnionAnnotation annotations
        return (let
            DataOffset Size16 tagOffset = structFieldPackingMap parent ! number
            in DescUnion UnionDesc
            { unionName = name
            , unionParent = parent
            , unionNumber = number
            , unionTagOffset = tagOffset
            , unionFields = fields
            , unionAnnotations = compiledAnnotations
            , unionMemberMap = memberMap
            , unionMembers = members
            , unionFieldDiscriminantMap = discriminantMap
            })))
compileDecl _ (UnionDecl (Located pos name) _ _ _) =
    CompiledStatementStatus name (makeError pos "Unions can only appear inside structs.")

compileDecl scope
            (FieldDecl (Located pos name) (Located _ number) typeExp annotations defaultValue) =
    CompiledStatementStatus name (do
        parent <- case scope of
            DescStruct s -> return s
            DescUnion u -> return (unionParent u)
            _ -> makeError pos "Fields can only appear inside structs."
        let unionDesc = case scope of
                DescUnion u -> Just (u, unionFieldDiscriminantMap u ! number)
                _ -> Nothing
        typeDesc <- compileType scope typeExp
        recover () $ when (fieldSizeInBits (fieldSize typeDesc) > maxInlineFieldBits) $
            makeError pos $ printf "Inlined fields cannot exceed %d bytes."
            (div maxInlineFieldBits 8)
        defaultDesc <- case defaultValue of
            Just (Located defaultPos value) -> do
                result <- fmap Just (compileValue defaultPos typeDesc value)
                recover () (case typeDesc of
                    InlineStructType _ ->
                        makeError defaultPos "Inline fields cannot have default values."
                    InlineListType _ _ ->
                        makeError defaultPos "Inline fields cannot have default values."
                    InlineDataType _ ->
                        makeError defaultPos "Inline fields cannot have default values."
                    _ -> return ())
                return result
            Nothing -> return Nothing
        compiledAnnotations <- compileAnnotations scope FieldAnnotation annotations
        return (let
            in DescField FieldDesc
            { fieldName = name
            , fieldParent = parent
            , fieldNumber = number
            , fieldOffset = structFieldPackingMap parent ! number
            , fieldUnion = unionDesc
            , fieldType = typeDesc
            , fieldDefaultValue = defaultDesc
            , fieldAnnotations = compiledAnnotations
            }))

compileDecl scope (InterfaceDecl (Located _ name) maybeTypeId annotations decls) =
    CompiledStatementStatus name (feedback (\desc -> do
        (members, memberMap) <- compileChildDecls desc decls
        requireNoDuplicateNames decls
        let numbers = [ num | MethodDecl _ num _ _ _ <- decls ]
        requireSequentialNumbering "Methods" numbers
        requireOrdinalsInRange numbers
        compiledAnnotations <- compileAnnotations scope InterfaceAnnotation annotations
        return (DescInterface InterfaceDesc
            { interfaceName = name
            , interfaceId = childId name maybeTypeId scope
            , interfaceParent = scope
            , interfaceMethods = [d | DescMethod    d <- members]
            , interfaceAnnotations = compiledAnnotations
            , interfaceMemberMap = memberMap
            , interfaceMembers = members
            })))

compileDecl scope@(DescInterface parent)
            (MethodDecl (Located _ name) (Located _ number) params returnType annotations) =
    CompiledStatementStatus name (feedback (\desc -> do
        paramDescs <- doAll (map (compileParam desc) (zip [0..] params))
        returnTypeDesc <- compileType scope returnType
        compiledAnnotations <- compileAnnotations scope MethodAnnotation annotations
        return (DescMethod MethodDesc
            { methodName = name
            , methodParent = parent
            , methodNumber = number
            , methodParams = paramDescs
            , methodReturnType = returnTypeDesc
            , methodAnnotations = compiledAnnotations
            })))
compileDecl _ (MethodDecl (Located pos name) _ _ _ _) =
    CompiledStatementStatus name (makeError pos "Methods can only appear inside interfaces.")

compileDecl scope (AnnotationDecl (Located _ name) maybeTypeId typeExp annotations targets) =
    CompiledStatementStatus name (do
        typeDesc <- compileType scope typeExp
        compiledAnnotations <- compileAnnotations scope AnnotationAnnotation annotations
        return (DescAnnotation AnnotationDesc
            { annotationName = name
            , annotationId = childId name maybeTypeId scope
            , annotationParent = scope
            , annotationType = typeDesc
            , annotationAnnotations = compiledAnnotations
            , annotationTargets = Set.fromList targets
            }))

compileParam scope@(DescMethod parent)
             (ordinal, ParamDecl name typeExp annotations defaultValue) = do
    typeDesc <- compileType scope typeExp
    defaultDesc <- case defaultValue of
        Just (Located pos value) -> fmap Just (compileValue pos typeDesc value)
        Nothing -> return Nothing
    compiledAnnotations <- compileAnnotations scope ParamAnnotation annotations
    return ParamDesc
        { paramName = name
        , paramParent = parent
        , paramNumber = ordinal
        , paramType = typeDesc
        , paramDefaultValue = defaultDesc
        , paramAnnotations = compiledAnnotations
        }
compileParam _ _ = error "scope of parameter was not a method"

compileFile name theId decls annotations importMap =
    feedback (\desc -> do
        (members, memberMap) <- compileChildDecls (DescFile desc) decls
        requireNoDuplicateNames decls
        compiledAnnotations <- compileAnnotations (DescFile desc) FileAnnotation annotations
        return FileDesc
            { fileName = name
            , fileId = locatedValue theId
            , fileImports = Map.elems importMap
            , fileRuntimeImports =
                Set.fromList $ map fileName $ concatMap descRuntimeImports members
            , fileAnnotations = compiledAnnotations
            , fileMemberMap = memberMap
            , fileImportMap = importMap
            , fileMembers = members
            })

dedup :: Ord a => [a] -> [a]
dedup = Set.toList . Set.fromList

emptyFileDesc filename = FileDesc
    { fileName = filename
    , fileId = 0x0
    , fileImports = []
    , fileRuntimeImports = Set.empty
    , fileAnnotations = Map.empty
    , fileMemberMap = Map.empty
    , fileImportMap = Map.empty
    , fileMembers = []
    }

parseAndCompileFile :: Monad m
                    => FilePath                                -- Name of this file.
                    -> String                                  -- Content of this file.
                    -> (String -> m (Either FileDesc String))  -- Callback to import other files.
                    -> m Word64                                -- Callback to generate a random id.
                    -> m (Status FileDesc)                     -- Compiled file and/or errors.
parseAndCompileFile filename text importCallback randomCallback = do
    let (maybeFileId, decls, annotations, parseErrors) = parseFile filename text
        importNames = dedup $ concatMap declImports decls
        doImport (Located pos name) = do
            result <- importCallback name
            case result of
                Left desc -> return (succeed (name, desc))
                Right err -> return $ recover (name, emptyFileDesc name)
                    (makeError pos (printf "Couldn't import \"%s\": %s" name err))

    importStatuses <- mapM doImport importNames

    let dummyPos = newPos filename 1 1
    theFileId <- case maybeFileId of
        Nothing -> liftM (Located dummyPos) randomCallback
        Just i -> return i

    return (do
        -- We are now in the Status monad.

        -- Report errors from parsing.
        -- We do the compile step even if there were errors in parsing, and just combine all the
        -- errors together.  This may allow the user to fix more errors per compiler iteration, but
        -- it might also be confusing if a parse error causes a subsequent compile error,
        -- especially if the compile error ends up being on a line before the parse error (e.g.
        -- there's a parse error in a type definition, causing a not-defined error on a field
        -- trying to use that type).
        -- TODO:  Re-evaluate after getting some experience on whether this is annoing.
        Active () parseErrors

        -- Report errors from imports.
        -- Similar to the above, we're continuing with compiling even if imports fail, but the
        -- problem above probably doesn't occur in this case since global imports usually appear
        -- at the top of the file anyway.  The only annoyance is seeing a long error log because
        -- of one bad import.
        imports <- doAll importStatuses

        -- Report lack of an id.
        when (isNothing maybeFileId) $
            makeError dummyPos $
                printf "File does not declare an ID.  I've generated one for you.  Add this line \
                       \to your file: @0x%016x;" (locatedValue theFileId)

        -- Compile the file!
        compileFile filename theFileId decls annotations $ Map.fromList imports)
