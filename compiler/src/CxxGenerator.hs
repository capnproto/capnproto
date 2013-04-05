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
cxxTypeString (EnumType desc) = enumName desc               -- TODO: full name
cxxTypeString (StructType desc) = structName desc           -- TODO: full name
cxxTypeString (InterfaceType desc) = interfaceName desc     -- TODO: full name
cxxTypeString (ListType t) = concat [" ::capnproto::List<", cxxTypeString t, ">"]

cxxFieldSizeString Size0 = "VOID";
cxxFieldSizeString Size1 = "BIT";
cxxFieldSizeString Size8 = "BYTE";
cxxFieldSizeString Size16 = "TWO_BYTES";
cxxFieldSizeString Size32 = "FOUR_BYTES";
cxxFieldSizeString Size64 = "EIGHT_BYTES";
cxxFieldSizeString SizeReference = "REFERENCE";
cxxFieldSizeString (SizeInlineComposite _ _) = "INLINE_COMPOSITE";

cxxValueString VoidDesc = " ::capnproto::Void::VOID"
cxxValueString (BoolDesc    b) = if b then "true" else "false"
cxxValueString (Int8Desc    i) = show i
cxxValueString (Int16Desc   i) = show i
cxxValueString (Int32Desc   i) = show i
cxxValueString (Int64Desc   i) = show i ++ "ll"
cxxValueString (UInt8Desc   i) = show i
cxxValueString (UInt16Desc  i) = show i
cxxValueString (UInt32Desc  i) = show i ++ "u"
cxxValueString (UInt64Desc  i) = show i ++ "llu"
cxxValueString (Float32Desc x) = show x ++ "f"
cxxValueString (Float64Desc x) = show x
cxxValueString (EnumValueValueDesc v) =
    cxxTypeString (EnumType $ enumValueParent v) ++ "::" ++
    toUpperCaseWithUnderscores (enumValueName v)
cxxValueString (TextDesc _) = error "No default value literal for aggregate type."
cxxValueString (DataDesc _) = error "No default value literal for aggregate type."
cxxValueString (StructValueDesc _) = error "No default value literal for aggregate type."
cxxValueString (ListDesc _) = error "No default value literal for aggregate type."

defaultValueBytes _ (TextDesc s) = Just (UTF8.encode s ++ [0])
defaultValueBytes _ (DataDesc d) = Just d
defaultValueBytes t v@(StructValueDesc _) = Just $ encodeMessage t v
defaultValueBytes t v@(ListDesc _) = Just $ encodeMessage t v
defaultValueBytes _ _ = Nothing

cxxDefaultDefault (BuiltinType BuiltinVoid) = " ::capnproto::Void::VOID"
cxxDefaultDefault (BuiltinType BuiltinBool) = "false"
cxxDefaultDefault (BuiltinType BuiltinInt8) = "0"
cxxDefaultDefault (BuiltinType BuiltinInt16) = "0"
cxxDefaultDefault (BuiltinType BuiltinInt32) = "0"
cxxDefaultDefault (BuiltinType BuiltinInt64) = "0"
cxxDefaultDefault (BuiltinType BuiltinUInt8) = "0"
cxxDefaultDefault (BuiltinType BuiltinUInt16) = "0"
cxxDefaultDefault (BuiltinType BuiltinUInt32) = "0"
cxxDefaultDefault (BuiltinType BuiltinUInt64) = "0"
cxxDefaultDefault (BuiltinType BuiltinFloat32) = "0"
cxxDefaultDefault (BuiltinType BuiltinFloat64) = "0"
cxxDefaultDefault (BuiltinType BuiltinText) = "\"\""
cxxDefaultDefault (EnumType desc) = cxxValueString $ EnumValueValueDesc $ head $ enumValues desc
cxxDefaultDefault (BuiltinType BuiltinData) = error "No default value literal for aggregate type."
cxxDefaultDefault (StructType _) = error "No default value literal for aggregate type."
cxxDefaultDefault (InterfaceType _) = error "No default value literal for aggregate type."
cxxDefaultDefault (ListType _) = error "No default value literal for aggregate type."

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

fieldContext parent desc = mkStrContext context where
    context "fieldName" = MuVariable $ fieldName desc
    context "fieldDecl" = MuVariable $ descToCode "" (DescField desc)
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
            Just v -> MuList [defaultBytesContext context (fieldType desc) v]
            Nothing -> muNull
    context "fieldType" = MuVariable $ cxxTypeString $ fieldType desc
    context "fieldBlobType" = MuVariable $ blobTypeString $ fieldType desc
    context "fieldOffset" = MuVariable $ fieldOffset desc
    context "fieldDefaultValue" = case fieldDefaultValue desc of
        Just v -> MuVariable $ cxxValueString v
        Nothing -> MuVariable $ cxxDefaultDefault $ fieldType desc
    context "fieldElementSize" =
        MuVariable $ cxxFieldSizeString $ elementSize $ elementType $ fieldType desc
    context "fieldElementType" =
        MuVariable $ cxxTypeString $ elementType $ fieldType desc
    context s = parent s

structContext parent desc = mkStrContext context where
    context "structName" = MuVariable $ structName desc
    context "structFields" = MuList $ map (fieldContext context) $ structFields desc
    context "structDataSize" = MuVariable $ packingDataSize $ structPacking desc
    context "structReferenceCount" = MuVariable $ packingReferenceCount $ structPacking desc
    context "structChildren" = MuList []  -- TODO
    context "structDefault" = MuList [defaultBytesContext context (StructType desc)
        (encodeMessage (StructType desc) (StructValueDesc []))]
    context s = parent s

fileContext desc = mkStrContext context where
    context "fileName" = MuVariable $ fileName desc
    context "fileBasename" = MuVariable $ takeBaseName $ fileName desc
    context "fileIncludeGuard" = MuVariable $
        "CAPNPROTO_INCLUDED_" ++ hashString (fileName desc)
    context "fileNamespaces" = MuList []  -- TODO
    context "fileEnums" = MuList $ map (enumContext context) $ fileEnums desc
    context "fileStructs" = MuList $ map (structContext context) $ fileStructs desc
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
