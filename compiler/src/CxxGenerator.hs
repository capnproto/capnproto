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

{-# LANGUAGE TemplateHaskell #-}

module CxxGenerator(generateCxx) where

import qualified Data.ByteString.UTF8 as ByteStringUTF8
import Data.FileEmbed(embedFile)
import Data.Word(Word8)
import qualified Data.Digest.MD5 as MD5
import qualified Data.Map as Map
import qualified Data.Set as Set
import qualified Data.List as List
import Data.Maybe(catMaybes, mapMaybe)
import Data.Binary.IEEE754(floatToWord, doubleToWord)
import Data.Map((!))
import Data.Function(on)
import Text.Printf(printf)
import Text.Hastache
import Text.Hastache.Context
import qualified Codec.Binary.UTF8.String as UTF8
import System.FilePath(takeBaseName)

import Semantics
import Util
import WireFormat

-- MuNothing isn't considered a false value for the purpose of {{#variable}} expansion.  Use this
-- instead.
muNull = MuBool False;

-- There is no way to make a MuType from a singular MuContext, i.e. for optional sub-contexts.
-- Using a single-element list has the same effect, though.
muJust c = MuList [c]

namespaceAnnotationId = 0xb9c6f99ebf805f2c

fileNamespace desc = fmap testAnnotation $ Map.lookup namespaceAnnotationId $ fileAnnotations desc

testAnnotation (_, TextDesc x) = x
testAnnotation (desc, _) =
    error "Annotation was supposed to be text, but wasn't: " ++ annotationName desc

fullName desc = scopePrefix (descParent desc) ++ descName desc

scopePrefix (DescFile _) = ""
scopePrefix desc = fullName desc ++ "::"

globalName (DescFile desc) = maybe " " (" ::" ++) $ fileNamespace desc
globalName desc = globalName (descParent desc) ++ "::" ++ descName desc

-- Flatten the descriptor tree in pre-order, returning struct, union, and interface descriptors
-- only.
flattenTypes :: [Desc] -> [Desc]
flattenTypes [] = []
flattenTypes (d@(DescStruct s):rest) = d:(flattenTypes children ++ flattenTypes rest) where
    children = catMaybes $ Map.elems $ structMemberMap s
flattenTypes (d@(DescUnion u):rest) = d:(flattenTypes children ++ flattenTypes rest) where
    children = catMaybes $ Map.elems $ unionMemberMap u
flattenTypes (d@(DescInterface i):rest) = d:(flattenTypes children ++ flattenTypes rest) where
    children = catMaybes $ Map.elems $ interfaceMemberMap i
flattenTypes (d@(DescEnum _):rest) = d:flattenTypes rest
flattenTypes (_:rest) = flattenTypes rest

hashString :: String -> String
hashString str =
    concatMap (printf "%02x" . fromEnum) $
    MD5.hash $
    UTF8.encode str

isPrimitive (BuiltinType BuiltinObject) = False
isPrimitive t@(BuiltinType _) = not $ isBlob t
isPrimitive (EnumType _) = True
isPrimitive (StructType _) = False
isPrimitive (InlineStructType _) = False
isPrimitive (InterfaceType _) = False
isPrimitive (ListType _) = False
isPrimitive (InlineListType _ _) = False
isPrimitive (InlineDataType _) = False

isBlob (BuiltinType BuiltinText) = True
isBlob (BuiltinType BuiltinData) = True
isBlob (InlineDataType _) = True
isBlob _ = False

isInlineBlob (InlineDataType _) = True
isInlineBlob _ = False

isStruct (StructType _) = True
isStruct (InlineStructType _) = True
isStruct _ = False

isInlineStruct (InlineStructType _) = True
isInlineStruct _ = False

isList (ListType _) = True
isList (InlineListType _ _) = True
isList _ = False

isNonStructList (ListType t) = not $ isStruct t
isNonStructList (InlineListType t _) = not $ isStruct t
isNonStructList _ = False

isPrimitiveList (ListType t) = isPrimitive t
isPrimitiveList (InlineListType t _) = isPrimitive t
isPrimitiveList _ = False

isPointerElement (InlineDataType _) = False
isPointerElement t = not (isPrimitive t || isStruct t || isInlineList t)

isPointerList (ListType t) = isPointerElement t
isPointerList (InlineListType t _) = isPointerElement t
isPointerList _ = False

isInlineBlobList (ListType t) = isInlineBlob t
isInlineBlobList _ = False

isStructList (ListType t@(InlineListType _ _)) = isStructList t
isStructList (InlineListType t@(InlineListType _ _) _) = isStructList t
isStructList (ListType t) = isStruct t
isStructList (InlineListType t _) = isStruct t
isStructList _ = False

isInlineList (InlineListType _ _) = True
isInlineList _ = False

isGenericObject (BuiltinType BuiltinObject) = True
isGenericObject _ = False

blobTypeString (BuiltinType BuiltinText) = "Text"
blobTypeString (BuiltinType BuiltinData) = "Data"
blobTypeString (InlineDataType _) = "Data"
blobTypeString (ListType t) = blobTypeString t
blobTypeString (InlineListType t _) = blobTypeString t
blobTypeString _ = error "Not a blob."

inlineMultiplier (InlineListType t s) = s * inlineMultiplier t
inlineMultiplier (InlineDataType s) = s
inlineMultiplier _ = 1

listInlineMultiplierString (ListType t) = case inlineMultiplier t of
    1 -> ""
    s -> " * " ++ show s
listInlineMultiplierString _ = error "Not a list."

cxxTypeString (BuiltinType BuiltinVoid) = " ::capnproto::Void"
cxxTypeString (BuiltinType BuiltinBool) = "bool"
cxxTypeString (BuiltinType BuiltinInt8) = " ::int8_t"
cxxTypeString (BuiltinType BuiltinInt16) = " ::int16_t"
cxxTypeString (BuiltinType BuiltinInt32) = " ::int32_t"
cxxTypeString (BuiltinType BuiltinInt64) = " ::int64_t"
cxxTypeString (BuiltinType BuiltinUInt8) = " ::uint8_t"
cxxTypeString (BuiltinType BuiltinUInt16) = " ::uint16_t"
cxxTypeString (BuiltinType BuiltinUInt32) = " ::uint32_t"
cxxTypeString (BuiltinType BuiltinUInt64) = " ::uint64_t"
cxxTypeString (BuiltinType BuiltinFloat32) = "float"
cxxTypeString (BuiltinType BuiltinFloat64) = "double"
cxxTypeString (BuiltinType BuiltinText) = " ::capnproto::Text"
cxxTypeString (BuiltinType BuiltinData) = " ::capnproto::Data"
cxxTypeString (BuiltinType BuiltinObject) = " ::capnproto::Object"
cxxTypeString (EnumType desc) = globalName $ DescEnum desc
cxxTypeString (StructType desc) = globalName $ DescStruct desc
cxxTypeString (InlineStructType desc) = globalName $ DescStruct desc
cxxTypeString (InterfaceType desc) = globalName $ DescInterface desc
cxxTypeString (ListType t) = concat [" ::capnproto::List<", cxxTypeString t, ">"]
cxxTypeString (InlineListType t s) =
    concat [" ::capnproto::InlineList<", cxxTypeString t, ", ", show s, ">"]
cxxTypeString (InlineDataType s) =
    concat [" ::capnproto::InlineData<", show s, ">"]

cxxFieldSizeString SizeVoid = "VOID";
cxxFieldSizeString (SizeData Size1) = "BIT";
cxxFieldSizeString (SizeData Size8) = "BYTE";
cxxFieldSizeString (SizeData Size16) = "TWO_BYTES";
cxxFieldSizeString (SizeData Size32) = "FOUR_BYTES";
cxxFieldSizeString (SizeData Size64) = "EIGHT_BYTES";
cxxFieldSizeString SizePointer = "POINTER";
cxxFieldSizeString (SizeInlineComposite _ _) = "INLINE_COMPOSITE";

fieldOffsetInteger VoidOffset = "0"
fieldOffsetInteger (DataOffset _ o) = show o
fieldOffsetInteger (PointerOffset o) = show o
fieldOffsetInteger (InlineCompositeOffset d p ds ps) = let
    byteSize = div (dataSectionBits ds) 8
    byteOffset = case ds of
        DataSectionWords _ -> d * 8
        _ -> d * byteSize
    in printf "%d * ::capnproto::BYTES, %d * ::capnproto::BYTES, \
              \%d * ::capnproto::POINTERS, %d * ::capnproto::POINTERS" byteOffset byteSize p ps

isDefaultZero VoidDesc = True
isDefaultZero (BoolDesc    b) = not b
isDefaultZero (Int8Desc    i) = i == 0
isDefaultZero (Int16Desc   i) = i == 0
isDefaultZero (Int32Desc   i) = i == 0
isDefaultZero (Int64Desc   i) = i == 0
isDefaultZero (UInt8Desc   i) = i == 0
isDefaultZero (UInt16Desc  i) = i == 0
isDefaultZero (UInt32Desc  i) = i == 0
isDefaultZero (UInt64Desc  i) = i == 0
isDefaultZero (Float32Desc x) = x == 0
isDefaultZero (Float64Desc x) = x == 0
isDefaultZero (EnumerantValueDesc v) = enumerantNumber v == 0
isDefaultZero (TextDesc _) = error "Can't call isDefaultZero on aggregate types."
isDefaultZero (DataDesc _) = error "Can't call isDefaultZero on aggregate types."
isDefaultZero (StructValueDesc _) = error "Can't call isDefaultZero on aggregate types."
isDefaultZero (ListDesc _) = error "Can't call isDefaultZero on aggregate types."

defaultMask VoidDesc = "0"
defaultMask (BoolDesc    b) = if b then "true" else "false"
defaultMask (Int8Desc    i) = show i
defaultMask (Int16Desc   i) = show i
defaultMask (Int32Desc   i) = show i
defaultMask (Int64Desc   i) = show i ++ "ll"
defaultMask (UInt8Desc   i) = show i
defaultMask (UInt16Desc  i) = show i
defaultMask (UInt32Desc  i) = show i ++ "u"
defaultMask (UInt64Desc  i) = show i ++ "llu"
defaultMask (Float32Desc x) = show (floatToWord x) ++ "u"
defaultMask (Float64Desc x) = show (doubleToWord x) ++ "ul"
defaultMask (EnumerantValueDesc v) = show (enumerantNumber v)
defaultMask (TextDesc _) = error "Can't call defaultMask on aggregate types."
defaultMask (DataDesc _) = error "Can't call defaultMask on aggregate types."
defaultMask (StructValueDesc _) = error "Can't call defaultMask on aggregate types."
defaultMask (ListDesc _) = error "Can't call defaultMask on aggregate types."

defaultValueBytes _ (TextDesc s) = Just (UTF8.encode s ++ [0])
defaultValueBytes _ (DataDesc d) = Just d
defaultValueBytes t v@(StructValueDesc _) = Just $ encodeMessage t v
defaultValueBytes t v@(ListDesc _) = Just $ encodeMessage t v
defaultValueBytes _ _ = Nothing

elementType (ListType t) = t
elementType (InlineListType t _) = t
elementType _ = error "Called elementType on non-list."

inlineElementType (ListType t@(InlineListType _ _)) = inlineElementType t
inlineElementType (InlineListType t@(InlineListType _ _) _) = inlineElementType t
inlineElementType t = elementType t

repeatedlyTake _ [] = []
repeatedlyTake n l = take n l : repeatedlyTake n (drop n l)

typeDependencies (StructType s) = [structId s]
typeDependencies (EnumType e) = [enumId e]
typeDependencies (InterfaceType i) = [interfaceId i]
typeDependencies (ListType t) = typeDependencies t
typeDependencies _ = []

paramDependencies d = typeDependencies $ paramType d

descDependencies (DescStruct d) = concatMap descDependencies $ structMembers d
descDependencies (DescUnion d) = concatMap descDependencies $ unionMembers d
descDependencies (DescField d) = typeDependencies $ fieldType d
descDependencies (DescInterface d) = concatMap descDependencies $ interfaceMembers d
descDependencies (DescMethod d) =
    concat $ typeDependencies (methodReturnType d) : map paramDependencies (methodParams d)
descDependencies _ = []

memberIndexes :: Int -> [(Int, Int)]
memberIndexes unionIndex = zip (repeat unionIndex) [0..]

memberTable (DescStruct desc) = let
    -- Fields and unions of the struct.
    topMembers = zip (memberIndexes 0) $ mapMaybe memberName
               $ List.sortBy (compare `on` ordinal) $ structMembers desc

    -- Fields of each union.
    innerMembers = catMaybes $ zipWith indexedUnionMembers [1..]
                 $ List.sortBy (compare `on` ordinal) $ structMembers desc

    ordinal (DescField f) = fieldNumber f
    ordinal (DescUnion u) = unionNumber u
    ordinal _ = 65536  -- doesn't really matter what this is; will be filtered out later

    memberName (DescField f) = Just $ fieldName f
    memberName (DescUnion u) = Just $ unionName u
    memberName _ = Nothing

    indexedUnionMembers i (DescUnion u) =
        Just $ zip (memberIndexes i) $ mapMaybe memberName $
            List.sortBy (compare `on` ordinal) $ unionMembers u
    indexedUnionMembers _ _ = Nothing

    in concat $ topMembers : innerMembers

memberTable (DescEnum desc) = zip (memberIndexes 0) $ map enumerantName
    $ List.sortBy (compare `on` enumerantNumber) $ enumerants desc
memberTable (DescInterface desc) = zip (memberIndexes 0) $ map methodName
    $ List.sortBy (compare `on` methodNumber) $ interfaceMethods desc
memberTable _ = []

outerFileContext schemaNodes = fileContext where
    schemaDepContext parent i = mkStrContext context where
        context "dependencyId" = MuVariable (printf "%016x" i :: String)
        context s = parent s

    schemaMemberByNameContext parent (ui, mi) = mkStrContext context where
        context "memberUnionIndex" = MuVariable ui
        context "memberIndex" = MuVariable mi
        context s = parent s

    schemaContext parent desc = mkStrContext context where
        node = schemaNodes ! descId desc

        codeLines = map (delimit ", ") $ repeatedlyTake 8 $ map (printf "%3d") node

        depIds = map head $ List.group $ List.sort $ descDependencies desc

        membersByName = map fst $ List.sortBy (compare `on` memberByNameKey) $ memberTable desc
        memberByNameKey ((unionIndex, _), name) = (unionIndex, name)

        context "schemaWordCount" = MuVariable $ div (length node + 7) 8
        context "schemaBytes" = MuVariable $ delimit ",\n    " codeLines
        context "schemaId" = MuVariable (printf "%016x" (descId desc) :: String)
        context "schemaDependencyCount" = MuVariable $ length depIds
        context "schemaDependencies" =
            MuList $ map (schemaDepContext context) depIds
        context "schemaMemberCount" = MuVariable $ length membersByName
        context "schemaMembersByName" =
            MuList $ map (schemaMemberByNameContext context) membersByName
        context s = parent s

    enumerantContext parent desc = mkStrContext context where
        context "enumerantName" = MuVariable $ toUpperCaseWithUnderscores $ enumerantName desc
        context "enumerantNumber" = MuVariable $ enumerantNumber desc
        context s = parent s

    enumContext parent desc = mkStrContext context where
        context "enumName" = MuVariable $ enumName desc
        context "enumId" = MuVariable (printf "%016x" (enumId desc) ::String)
        context "enumerants" = MuList $ map (enumerantContext context) $ enumerants desc
        context s = parent s

    defaultBytesContext :: Monad m => (String -> MuType m) -> TypeDesc -> [Word8] -> MuContext m
    defaultBytesContext parent t bytes = mkStrContext context where
        codeLines = map (delimit ", ") $ repeatedlyTake 8 $ map (printf "%3d") bytes
        context "defaultByteList" = MuVariable $ delimit ",\n    " codeLines
        context "defaultWordCount" = MuVariable $ div (length bytes + 7) 8
        context "defaultBlobSize" = case t of
            BuiltinType BuiltinText -> MuVariable (length bytes - 1)  -- Don't include NUL terminator.
            BuiltinType BuiltinData -> MuVariable (length bytes)
            _ -> error "defaultBlobSize used on non-blob."
        context s = parent s

    descDecl desc = head $ lines $ descToCode "" desc

    fieldContext parent desc = mkStrContext context where
        context "fieldName" = MuVariable $ fieldName desc
        context "fieldDecl" = MuVariable $ descDecl $ DescField desc
        context "fieldTitleCase" = MuVariable $ toTitleCase $ fieldName desc
        context "fieldUpperCase" = MuVariable $ toUpperCaseWithUnderscores $ fieldName desc
        context "fieldIsPrimitive" = MuBool $ isPrimitive $ fieldType desc

        context "fieldIsListOrBlob" = MuBool $ isBlob (fieldType desc) || isList (fieldType desc)

        context "fieldIsBlob" = MuBool $ isBlob $ fieldType desc
        context "fieldIsInlineBlob" = MuBool $ isInlineBlob $ fieldType desc
        context "fieldIsStruct" = MuBool $ isStruct $ fieldType desc
        context "fieldIsInlineStruct" = MuBool $ isInlineStruct $ fieldType desc
        context "fieldIsList" = MuBool $ isList $ fieldType desc
        context "fieldIsNonStructList" = MuBool $ isNonStructList $ fieldType desc
        context "fieldIsPrimitiveList" = MuBool $ isPrimitiveList $ fieldType desc
        context "fieldIsPointerList" = MuBool $ isPointerList $ fieldType desc
        context "fieldIsInlineBlobList" = MuBool $ isInlineBlobList $ fieldType desc
        context "fieldIsStructList" = MuBool $ isStructList $ fieldType desc
        context "fieldIsInlineList" = MuBool $ isInlineList $ fieldType desc
        context "fieldIsGenericObject" = MuBool $ isGenericObject $ fieldType desc
        context "fieldDefaultBytes" =
            case fieldDefaultValue desc >>= defaultValueBytes (fieldType desc) of
                Just v -> muJust $ defaultBytesContext context (fieldType desc) v
                Nothing -> muNull
        context "fieldType" = MuVariable $ cxxTypeString $ fieldType desc
        context "fieldBlobType" = MuVariable $ blobTypeString $ fieldType desc
        context "fieldOffset" = MuVariable $ fieldOffsetInteger $ fieldOffset desc
        context "fieldInlineListSize" = case fieldType desc of
            InlineListType _ n -> MuVariable n
            InlineDataType n -> MuVariable n
            _ -> muNull
        context "fieldInlineDataOffset" = case fieldOffset desc of
            InlineCompositeOffset off _ size _ ->
                MuVariable (off * div (dataSizeInBits (dataSectionAlignment size)) 8)
            _ -> muNull
        context "fieldInlineDataSize" = case fieldOffset desc of
            InlineCompositeOffset _ _ size _ ->
                MuVariable $ div (dataSectionBits size) 8
            _ -> muNull
        context "fieldInlinePointerOffset" = case fieldOffset desc of
            InlineCompositeOffset _ off _ _ -> MuVariable off
            _ -> muNull
        context "fieldInlinePointerSize" = case fieldOffset desc of
            InlineCompositeOffset _ _ _ size -> MuVariable size
            _ -> muNull
        context "fieldInlineMultiplier" = MuVariable $ listInlineMultiplierString $ fieldType desc
        context "fieldDefaultMask" = case fieldDefaultValue desc of
            Nothing -> MuVariable ""
            Just v -> MuVariable (if isDefaultZero v then "" else ", " ++ defaultMask v)
        context "fieldElementSize" =
            MuVariable $ cxxFieldSizeString $ fieldSize $ inlineElementType $ fieldType desc
        context "fieldElementType" =
            MuVariable $ cxxTypeString $ elementType $ fieldType desc
        context "fieldElementReaderType" = MuVariable readerString where
            readerString = if isPrimitiveList $ fieldType desc
                then tString
                else tString ++ "::Reader"
            tString = cxxTypeString $ elementType $ fieldType desc
        context "fieldInlineElementType" =
            MuVariable $ cxxTypeString $ inlineElementType $ fieldType desc
        context "fieldUnion" = case fieldUnion desc of
            Just (u, _) -> muJust $ unionContext context u
            Nothing -> muNull
        context "fieldUnionDiscriminant" = case fieldUnion desc of
            Just (_, n) -> MuVariable n
            Nothing -> muNull
        context "fieldSetterDefault" = case fieldType desc of
            BuiltinType BuiltinVoid -> MuVariable " = ::capnproto::Void::VOID"
            _ -> MuVariable ""
        context s = parent s

    unionContext parent desc = mkStrContext context where
        titleCase = toTitleCase $ unionName desc

        context "typeStruct" = MuBool False
        context "typeUnion" = MuBool True
        context "typeName" = MuVariable titleCase
        context "typeFullName" = context "unionFullName"
        context "typeFields" = context "unionFields"

        context "unionName" = MuVariable $ unionName desc
        context "unionFullName" = MuVariable $ fullName (DescStruct $ unionParent desc) ++
                                 "::" ++ titleCase
        context "unionDecl" = MuVariable $ descDecl $ DescUnion desc
        context "unionTitleCase" = MuVariable titleCase
        context "unionTagOffset" = MuVariable $ unionTagOffset desc
        context "unionFields" = MuList $ map (fieldContext context) $ unionFields desc
        context s = parent s

    childContext parent name = mkStrContext context where
        context "nestedName" = MuVariable name
        context s = parent s

    structContext parent desc = mkStrContext context where
        context "typeStruct" = MuBool True
        context "typeUnion" = MuBool False
        context "typeName" = context "structName"
        context "typeFullName" = context "structFullName"
        context "typeFields" = context "structFields"

        context "structName" = MuVariable $ structName desc
        context "structId" = MuVariable (printf "%016x" (structId desc) ::String)
        context "structFullName" = MuVariable $ fullName (DescStruct desc)
        context "structFields" = MuList $ map (fieldContext context) $ structFields desc
        context "structUnions" = MuList $ map (unionContext context) $ structUnions desc
        context "structDataSize" = MuVariable $ dataSectionWordSize $ structDataSize desc
        context "structPointerCount" = MuVariable $ structPointerCount desc
        context "structPreferredListEncoding" = case (structDataSize desc, structPointerCount desc) of
            (DataSectionWords 0, 0) -> MuVariable "VOID"
            (DataSection1, 0) -> MuVariable "BIT"
            (DataSection8, 0) -> MuVariable "BYTE"
            (DataSection16, 0) -> MuVariable "TWO_BYTES"
            (DataSection32, 0) -> MuVariable "FOUR_BYTES"
            (DataSectionWords 1, 0) -> MuVariable "EIGHT_BYTES"
            (DataSectionWords 0, 1) -> MuVariable "POINTER"
            _ -> MuVariable "INLINE_COMPOSITE"
        context "structNestedEnums" =
            MuList $ map (enumContext context) [m | DescEnum m <- structMembers desc]
        context "structNestedStructs" =
            MuList $ map (childContext context . structName) [m | DescStruct m <- structMembers desc]
        context "structNestedInterfaces" =
            MuList $ map (childContext context . interfaceName) [m | DescInterface m <- structMembers desc]
        context s = parent s

    typeContext parent desc = mkStrContext context where
        context "typeStructOrUnion" = case desc of
            DescStruct d -> muJust $ structContext context d
            DescUnion u -> muJust $ unionContext context u
            _ -> muNull
        context "typeEnum" = case desc of
            DescEnum d -> muJust $ enumContext context d
            _ -> muNull
        context "typeSchema" = case desc of
            DescUnion _ -> muNull
            _ -> muJust $ schemaContext context desc
        context s = parent s

    importContext parent ('/':filename) = mkStrContext context where
        context "importFilename" = MuVariable filename
        context "importIsSystem" = MuBool True
        context s = parent s
    importContext parent filename = mkStrContext context where
        context "importFilename" = MuVariable filename
        context "importIsSystem" = MuBool False
        context s = parent s

    namespaceContext parent part = mkStrContext context where
        context "namespaceName" = MuVariable part
        context s = parent s

    fileContext desc = mkStrContext context where
        flattenedMembers = flattenTypes $ catMaybes $ Map.elems $ fileMemberMap desc

        namespace = maybe [] (splitOn "::") $ fileNamespace desc

        isImportUsed (_, dep) = Set.member (fileName dep) (fileRuntimeImports desc)

        context "fileName" = MuVariable $ fileName desc
        context "fileBasename" = MuVariable $ takeBaseName $ fileName desc
        context "fileIncludeGuard" = MuVariable $
            "CAPNPROTO_INCLUDED_" ++ hashString (fileName desc ++ ':':show (fileId desc))
        context "fileNamespaces" = MuList $ map (namespaceContext context) namespace
        context "fileEnums" = MuList $ map (enumContext context) [e | DescEnum e <- fileMembers desc]
        context "fileTypes" = MuList $ map (typeContext context) flattenedMembers
        context "fileImports" = MuList $ map (importContext context . fst)
                              $ filter isImportUsed $ Map.toList $ fileImportMap desc
        context s = error ("Template variable not defined: " ++ s)

headerTemplate :: String
headerTemplate = ByteStringUTF8.toString $(embedFile "src/c++-header.mustache")

srcTemplate :: String
srcTemplate = ByteStringUTF8.toString $(embedFile "src/c++-source.mustache")

-- Sadly it appears that hashtache requires access to the IO monad, even when template inclusion
-- is disabled.
hastacheConfig :: MuConfig IO
hastacheConfig = MuConfig
    { muEscapeFunc = emptyEscape
    , muTemplateFileDir = Nothing
    , muTemplateFileExt = Nothing
    , muTemplateRead = \_ -> return Nothing
    }

generateCxxHeader file schemaNodes =
    hastacheStr hastacheConfig (encodeStr headerTemplate) (outerFileContext schemaNodes file)
generateCxxSource file schemaNodes =
    hastacheStr hastacheConfig (encodeStr srcTemplate) (outerFileContext schemaNodes file)

generateCxx files _ schemaNodes = do
    let handleFile file = do
            header <- generateCxxHeader file schemaNodes
            source <- generateCxxSource file schemaNodes
            return [(fileName file ++ ".h", header), (fileName file ++ ".c++", source)]
    results <- mapM handleFile files
    return $ concat results
