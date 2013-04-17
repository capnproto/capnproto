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
import Data.Maybe(catMaybes)
import Data.Binary.IEEE754(floatToWord, doubleToWord)
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

fullName desc = scopePrefix (descParent desc) ++ descName desc

scopePrefix (DescFile _) = ""
scopePrefix desc = fullName desc ++ "::"

globalName (DescFile _) = " "  -- TODO: namespaces
globalName desc = globalName (descParent desc) ++ "::" ++ descName desc

-- Flatten the descriptor tree in pre-order, returning struct, union, and interface descriptors
-- only.  We skip enums because they are always declared directly in their parent scope.
flattenTypes :: [Desc] -> [Desc]
flattenTypes [] = []
flattenTypes (d@(DescStruct s):rest) = d:(flattenTypes children ++ flattenTypes rest) where
    children = catMaybes $ Map.elems $ structMemberMap s
flattenTypes (d@(DescUnion u):rest) = d:(flattenTypes children ++ flattenTypes rest) where
    children = catMaybes $ Map.elems $ unionMemberMap u
flattenTypes (d@(DescInterface i):rest) = d:(flattenTypes children ++ flattenTypes rest) where
    children = catMaybes $ Map.elems $ interfaceMemberMap i
flattenTypes (_:rest) = flattenTypes rest

hashString :: String -> String
hashString str =
    concatMap (printf "%02x" . fromEnum) $
    MD5.hash $
    UTF8.encode str

isPrimitive t@(BuiltinType _) = not $ isBlob t
isPrimitive (EnumType _) = True
isPrimitive (StructType _) = False
isPrimitive (InterfaceType _) = False
isPrimitive (ListType _) = False

isBlob (BuiltinType BuiltinText) = True
isBlob (BuiltinType BuiltinData) = True
isBlob _ = False

isStruct (StructType _) = True
isStruct _ = False

isList (ListType _) = True
isList _ = False

isNonStructList (ListType t) = not $ isStruct t
isNonStructList _ = False

isPrimitiveList (ListType t) = isPrimitive t
isPrimitiveList _ = False

isStructList (ListType t) = isStruct t
isStructList _ = False

blobTypeString (BuiltinType BuiltinText) = "Text"
blobTypeString (BuiltinType BuiltinData) = "Data"
blobTypeString _ = error "Not a blob."

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
cxxTypeString (EnumType desc) = globalName $ DescEnum desc
cxxTypeString (StructType desc) = globalName $ DescStruct desc
cxxTypeString (InterfaceType desc) = globalName $ DescInterface desc
cxxTypeString (ListType t) = concat [" ::capnproto::List<", cxxTypeString t, ">"]

cxxFieldSizeString Size0 = "VOID";
cxxFieldSizeString Size1 = "BIT";
cxxFieldSizeString Size8 = "BYTE";
cxxFieldSizeString Size16 = "TWO_BYTES";
cxxFieldSizeString Size32 = "FOUR_BYTES";
cxxFieldSizeString Size64 = "EIGHT_BYTES";
cxxFieldSizeString SizeReference = "REFERENCE";
cxxFieldSizeString (SizeInlineComposite _ _) = "INLINE_COMPOSITE";

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
isDefaultZero (EnumValueValueDesc v) = enumValueNumber v == 0
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
defaultMask (EnumValueValueDesc v) = show (enumValueNumber v)
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
elementType _ = error "Called elementType on non-list."

repeatedlyTake _ [] = []
repeatedlyTake n l = take n l : repeatedlyTake n (drop n l)

enumValueContext parent desc = mkStrContext context where
    context "enumValueName" = MuVariable $ toUpperCaseWithUnderscores $ enumValueName desc
    context "enumValueNumber" = MuVariable $ enumValueNumber desc
    context s = parent s

enumContext parent desc = mkStrContext context where
    context "enumName" = MuVariable $ enumName desc
    context "enumValues" = MuList $ map (enumValueContext context) $ enumValues desc
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
    context "fieldIsBlob" = MuBool $ isBlob $ fieldType desc
    context "fieldIsStruct" = MuBool $ isStruct $ fieldType desc
    context "fieldIsList" = MuBool $ isList $ fieldType desc
    context "fieldIsNonStructList" = MuBool $ isNonStructList $ fieldType desc
    context "fieldIsPrimitiveList" = MuBool $ isPrimitiveList $ fieldType desc
    context "fieldIsStructList" = MuBool $ isStructList $ fieldType desc
    context "fieldDefaultBytes" =
        case fieldDefaultValue desc >>= defaultValueBytes (fieldType desc) of
            Just v -> muJust $ defaultBytesContext context (fieldType desc) v
            Nothing -> muNull
    context "fieldType" = MuVariable $ cxxTypeString $ fieldType desc
    context "fieldBlobType" = MuVariable $ blobTypeString $ fieldType desc
    context "fieldOffset" = MuVariable $ fieldOffset desc
    context "fieldDefaultMask" = case fieldDefaultValue desc of
        Nothing -> MuVariable ""
        Just v -> MuVariable (if isDefaultZero v then "" else ", " ++ defaultMask v)
    context "fieldElementSize" =
        MuVariable $ cxxFieldSizeString $ elementSize $ elementType $ fieldType desc
    context "fieldElementType" =
        MuVariable $ cxxTypeString $ elementType $ fieldType desc
    context "fieldUnion" = case fieldUnion desc of
        Just (u, _) -> muJust $ unionContext context u
        Nothing -> muNull
    context "fieldUnionDiscriminant" = case fieldUnion desc of
        Just (_, n) -> MuVariable n
        Nothing -> muNull
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
    context "structFullName" = MuVariable $ fullName (DescStruct desc)
    context "structFields" = MuList $ map (fieldContext context) $ structFields desc
    context "structUnions" = MuList $ map (unionContext context) $ structUnions desc
    context "structDataSize" = MuVariable $ packingDataSize $ structPacking desc
    context "structReferenceCount" = MuVariable $ packingReferenceCount $ structPacking desc
    context "structNestedEnums" =
        MuList $ map (enumContext context) $ structNestedEnums desc
    context "structNestedStructs" =
        MuList $ map (childContext context . structName) $ structNestedStructs desc
    context "structNestedInterfaces" =
        MuList $ map (childContext context . interfaceName) $ structNestedInterfaces desc
    context s = parent s

typeContext parent desc = mkStrContext context where
    context "typeStructOrUnion" = case desc of
        DescStruct d -> muJust $ structContext context d
        DescUnion u -> muJust $ unionContext context u
        _ -> muNull
    context "typeEnum" = case desc of
        DescEnum d -> muJust $ enumContext context d
        _ -> muNull
    context s = parent s

fileContext desc = mkStrContext context where
    flattenedMembers = flattenTypes $ catMaybes $ Map.elems $ fileMemberMap desc

    context "fileName" = MuVariable $ fileName desc
    context "fileBasename" = MuVariable $ takeBaseName $ fileName desc
    context "fileIncludeGuard" = MuVariable $
        "CAPNPROTO_INCLUDED_" ++ hashString (fileName desc)
    context "fileNamespaces" = MuList []  -- TODO
    context "fileEnums" = MuList $ map (enumContext context) $ fileEnums desc
    context "fileTypes" = MuList $ map (typeContext context) flattenedMembers
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

generateCxxHeader file = hastacheStr hastacheConfig (encodeStr headerTemplate) (fileContext file)
generateCxxSource file = hastacheStr hastacheConfig (encodeStr srcTemplate) (fileContext file)

generateCxx file = do
    header <- generateCxxHeader file
    source <- generateCxxSource file
    return [(fileName file ++ ".h", header), (fileName file ++ ".c++", source)]
