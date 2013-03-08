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

import qualified Data.ByteString as ByteString
import qualified Data.ByteString.UTF8 as UTF8
import Data.FileEmbed(embedFile)
import Data.Char(ord)
import qualified Data.Digest.MD5 as MD5
import Text.Printf(printf)
import Text.Hastache
import Text.Hastache.Context

import Semantics
import Util

-- MuNothing isn't considered a false value for the purpose of {{#variable}} expansion.  Use this
-- instead.
muNull = MuBool False;

hashString :: String -> String
hashString str =
    concatMap (printf "%02x" . fromEnum) $
    MD5.hash $
    ByteString.unpack $
    UTF8.fromString str

isPrimitive (BuiltinType _) = True
isPrimitive (EnumType _) = True
isPrimitive (StructType _) = False
isPrimitive (InterfaceType _) = False
isPrimitive (ListType _) = False

isStruct (StructType _) = True
isStruct _ = False

isList (ListType _) = True
isList _ = False

isPrimitiveList (ListType t) = isPrimitive t
isPrimitiveList _ = False

isStructList (ListType t) = isStruct t
isStructList _ = False

cxxTypeString (BuiltinType BuiltinVoid) = "void"
cxxTypeString (BuiltinType BuiltinBool) = "bool"
cxxTypeString (BuiltinType BuiltinInt8) = "int8_t"
cxxTypeString (BuiltinType BuiltinInt16) = "int16_t"
cxxTypeString (BuiltinType BuiltinInt32) = "int32_t"
cxxTypeString (BuiltinType BuiltinInt64) = "int64_t"
cxxTypeString (BuiltinType BuiltinUInt8) = "uint8_t"
cxxTypeString (BuiltinType BuiltinUInt16) = "uint16_t"
cxxTypeString (BuiltinType BuiltinUInt32) = "uint32_t"
cxxTypeString (BuiltinType BuiltinUInt64) = "uint64_t"
cxxTypeString (BuiltinType BuiltinFloat32) = "float"
cxxTypeString (BuiltinType BuiltinFloat64) = "double"
cxxTypeString (BuiltinType BuiltinText) = "TODO"
cxxTypeString (BuiltinType BuiltinData) = "TODO"
cxxTypeString (EnumType desc) = enumName desc
cxxTypeString (StructType desc) = structName desc
cxxTypeString (InterfaceType desc) = interfaceName desc
cxxTypeString (ListType t) = concat ["::capnproto::List<", cxxTypeString t, ">"]

cxxFieldSizeString Size0 = "VOID";
cxxFieldSizeString Size1 = "BIT";
cxxFieldSizeString Size8 = "BYTE";
cxxFieldSizeString Size16 = "TWO_BYTES";
cxxFieldSizeString Size32 = "FOUR_BYTES";
cxxFieldSizeString Size64 = "EIGHT_BYTES";
cxxFieldSizeString SizeReference = "REFERENCE";
cxxFieldSizeString (SizeInlineComposite _ _) = "INLINE_COMPOSITE";

cEscape [] = []
cEscape (first:rest) = result where
    eRest = cEscape rest
    result = case first of
        '\a' -> '\\':'a':eRest
        '\b' -> '\\':'b':eRest
        '\f' -> '\\':'f':eRest
        '\n' -> '\\':'n':eRest
        '\r' -> '\\':'r':eRest
        '\t' -> '\\':'t':eRest
        '\v' -> '\\':'v':eRest
        '\'' -> '\\':'\'':eRest
        '\"' -> '\\':'\"':eRest
        '\\' -> '\\':'\\':eRest
        '?'  -> '\\':'?':eRest
        c | c < ' ' || c > '~' -> '\\':(printf "%03o" (ord c) ++ eRest)
        c    -> c:eRest

cxxValueString VoidDesc = error "Can't stringify void value."
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
cxxValueString (TextDesc    s) = "\"" ++ cEscape s ++ "\""
cxxValueString (DataDesc    _) = error "Data defaults are encoded as bytes."
cxxValueString (EnumValueValueDesc v) =
    cxxTypeString (EnumType $ enumValueParent v) ++ "::" ++
    toUpperCaseWithUnderscores (enumValueName v)
cxxValueString (StructValueDesc _) = error "Struct defaults are encoded as bytes."
cxxValueString (ListDesc _) = error "List defaults are encoded as bytes."

cxxDefaultDefault (BuiltinType BuiltinVoid) = error "Can't stringify void value."
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
cxxDefaultDefault (BuiltinType BuiltinData) = error "Data defaults are encoded as bytes."
cxxDefaultDefault (EnumType desc) = cxxValueString $ EnumValueValueDesc $ head $ enumValues desc
cxxDefaultDefault (StructType _) = error "Struct defaults are encoded as bytes."
cxxDefaultDefault (InterfaceType _) = error "Interfaces have no default value."
cxxDefaultDefault (ListType _) = error "List defaults are encoded as bytes."

elementType (ListType t) = t
elementType _ = error "Called elementType on non-list."

fieldContext parent desc = mkStrContext context where
    context "fieldName" = MuVariable $ fieldName desc
    context "fieldDecl" = MuVariable $ descToCode "" (DescField desc)
    context "fieldTitleCase" = MuVariable $ toTitleCase $ fieldName desc
    context "fieldUpperCase" = MuVariable $ toUpperCaseWithUnderscores $ fieldName desc
    context "fieldIsPrimitive" = MuBool $ isPrimitive $ fieldType desc
    context "fieldIsStruct" = MuBool $ isStruct $ fieldType desc
    context "fieldIsList" = MuBool $ isList $ fieldType desc
    context "fieldIsPrimitiveList" = MuBool $ isPrimitiveList $ fieldType desc
    context "fieldIsStructList" = MuBool $ isStructList $ fieldType desc
    context "fieldDefaultBytes" = muNull
    context "fieldType" = MuVariable $ cxxTypeString $ fieldType desc
    context "fieldOffset" = MuVariable $ fieldOffset desc
    context "fieldDefaultValue" = case fieldDefaultValue desc of
        Just v -> MuVariable $ cxxValueString v
        Nothing -> MuVariable $ cxxDefaultDefault $ fieldType desc
    context "fieldElementSize" =
        MuVariable $ cxxFieldSizeString $ fieldSize $ elementType $ fieldType desc
    context s = parent s

structContext parent desc = mkStrContext context where
    context "structName" = MuVariable $ structName desc
    context "structFields" = MuList $ map (fieldContext context) $ structFields desc
    context "structChildren" = MuList []  -- TODO
    context s = parent s

fileContext desc = mkStrContext context where
    context "fileName" = MuVariable $ fileName desc
    context "fileIncludeGuard" = MuVariable $
        "CAPNPROTO_INCLUDED_" ++ hashString (fileName desc)
    context "fileNamespaces" = MuList []  -- TODO
    context "fileStructs" = MuList $ map (structContext context) $ fileStructs desc
    context s = MuVariable $ concat ["@@@", s, "@@@"]

headerTemplate :: String
headerTemplate = UTF8.toString $(embedFile "src/c++-header.mustache")

-- Sadly it appears that hashtache requires access to the IO monad, even when template inclusion
-- is disabled.
hastacheConfig :: MuConfig IO
hastacheConfig = MuConfig
    { muEscapeFunc = emptyEscape
    , muTemplateFileDir = Nothing
    , muTemplateFileExt = Nothing
    , muTemplateRead = \_ -> return Nothing
    }

generateCxx file =
    hastacheStr hastacheConfig (encodeStr headerTemplate) (fileContext file)
