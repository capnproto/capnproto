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

module Semantics where

import qualified Data.Map as Map
import qualified Data.List as List
import qualified Data.Maybe as Maybe
import Data.Int (Int8, Int16, Int32, Int64)
import Data.Word (Word8, Word16, Word32, Word64)
import Data.Char (chr)
import Text.Printf(printf)
import Control.Monad(join)
import Util(delimit)

-- Field counts are 8-bit, therefore there cannot be more than 255 fields, therefore the max field
-- number is 254.
maxFieldNumber = 254 :: Integer

-- Limiting method counts is not as important technically, but obviously it would be insane to have
-- anywhere near 2^16 methods.
maxMethodNumber = 65534 :: Integer

type ByteString = [Word8]

data Desc = DescFile FileDesc
          | DescAlias AliasDesc
          | DescConstant ConstantDesc
          | DescEnum EnumDesc
          | DescEnumValue EnumValueDesc
          | DescStruct StructDesc
          | DescUnion UnionDesc
          | DescField FieldDesc
          | DescInterface InterfaceDesc
          | DescMethod MethodDesc
          | DescOption OptionDesc
          | DescBuiltinType BuiltinType
          | DescBuiltinList

descName (DescFile      _) = "(top-level)"
descName (DescAlias     d) = aliasName d
descName (DescConstant  d) = constantName d
descName (DescEnum      d) = enumName d
descName (DescEnumValue d) = enumValueName d
descName (DescStruct    d) = structName d
descName (DescUnion     d) = unionName d
descName (DescField     d) = fieldName d
descName (DescInterface d) = interfaceName d
descName (DescMethod    d) = methodName d
descName (DescOption    d) = optionName d
descName (DescBuiltinType d) = builtinTypeName d
descName DescBuiltinList = "List"

descParent (DescFile      _) = error "File descriptor has no parent."
descParent (DescAlias     d) = aliasParent d
descParent (DescConstant  d) = constantParent d
descParent (DescEnum      d) = enumParent d
descParent (DescEnumValue d) = DescEnum (enumValueParent d)
descParent (DescStruct    d) = structParent d
descParent (DescUnion     d) = DescStruct (unionParent d)
descParent (DescField     d) = DescStruct (fieldParent d)
descParent (DescInterface d) = interfaceParent d
descParent (DescMethod    d) = DescInterface (methodParent d)
descParent (DescOption    d) = optionParent d
descParent (DescBuiltinType _) = error "Builtin type has no parent."
descParent DescBuiltinList = error "Builtin type has no parent."

type MemberMap = Map.Map String (Maybe Desc)

lookupMember :: String -> MemberMap -> Maybe Desc
lookupMember name members = join (Map.lookup name members)

data BuiltinType = BuiltinVoid | BuiltinBool
                 | BuiltinInt8 | BuiltinInt16 | BuiltinInt32 | BuiltinInt64
                 | BuiltinUInt8 | BuiltinUInt16 | BuiltinUInt32 | BuiltinUInt64
                 | BuiltinFloat32 | BuiltinFloat64
                 | BuiltinText | BuiltinData
                 deriving (Show, Enum, Bounded, Eq)

builtinTypes = [minBound::BuiltinType .. maxBound::BuiltinType]

-- Get in-language name of type.
builtinTypeName :: BuiltinType -> String
builtinTypeName = Maybe.fromJust . List.stripPrefix "Builtin" . show

data ValueDesc = VoidDesc
               | BoolDesc Bool
               | Int8Desc Int8
               | Int16Desc Int16
               | Int32Desc Int32
               | Int64Desc Int64
               | UInt8Desc Word8
               | UInt16Desc Word16
               | UInt32Desc Word32
               | UInt64Desc Word64
               | Float32Desc Float
               | Float64Desc Double
               | TextDesc String
               | DataDesc ByteString
               | EnumValueValueDesc EnumValueDesc
               | StructValueDesc [(FieldDesc, ValueDesc)]
               | ListDesc [ValueDesc]
               deriving (Show)

valueString VoidDesc = error "Can't stringify void value."
valueString (BoolDesc    b) = if b then "true" else "false"
valueString (Int8Desc    i) = show i
valueString (Int16Desc   i) = show i
valueString (Int32Desc   i) = show i
valueString (Int64Desc   i) = show i
valueString (UInt8Desc   i) = show i
valueString (UInt16Desc  i) = show i
valueString (UInt32Desc  i) = show i
valueString (UInt64Desc  i) = show i
valueString (Float32Desc x) = show x
valueString (Float64Desc x) = show x
valueString (TextDesc    s) = show s
valueString (DataDesc    s) = show (map (chr . fromIntegral) s)
valueString (EnumValueValueDesc v) = enumValueName v
valueString (StructValueDesc l) = "(" ++  delimit ", " (map assignmentString l) ++ ")" where
    assignmentString (field, value) = fieldName field ++ " = " ++ valueString value
valueString (ListDesc l) = "[" ++ delimit ", " (map valueString l) ++ "]" where

data TypeDesc = BuiltinType BuiltinType
              | EnumType EnumDesc
              | StructType StructDesc
              | InterfaceType InterfaceDesc
              | ListType TypeDesc

data PackingState = PackingState
    { packingHole1 :: Integer
    , packingHole8 :: Integer
    , packingHole16 :: Integer
    , packingHole32 :: Integer
    , packingDataSize :: Integer
    , packingReferenceCount :: Integer
    }

-- Represents the current packing state of a union.  The parameters are:
-- - The offset of a 64-bit word in the data segment allocated to the union.
-- - The offset of a reference allocated to the union.
-- - The offset of a smaller piece of the data segment allocated to the union.  Such a smaller
--   piece exists if one field in the union has lower number than the union itself -- in this case,
--   this is the piece that had been allocated to that field, and is now retroactively part of the
--   union.
data UnionPackingState = UnionPackingState
    { unionPackDataOffset :: Maybe Integer
    , unionPackReferenceOffset :: Maybe Integer
    , unionPackRetroactiveSlot :: Maybe (Integer, FieldSize)
    }

data FieldSize = Size0 | Size1 | Size8 | Size16 | Size32 | Size64 | SizeReference
               | SizeInlineComposite Integer Integer

fieldSize (BuiltinType BuiltinVoid) = Size0
fieldSize (BuiltinType BuiltinBool) = Size1
fieldSize (BuiltinType BuiltinInt8) = Size8
fieldSize (BuiltinType BuiltinInt16) = Size16
fieldSize (BuiltinType BuiltinInt32) = Size32
fieldSize (BuiltinType BuiltinInt64) = Size64
fieldSize (BuiltinType BuiltinUInt8) = Size8
fieldSize (BuiltinType BuiltinUInt16) = Size16
fieldSize (BuiltinType BuiltinUInt32) = Size32
fieldSize (BuiltinType BuiltinUInt64) = Size64
fieldSize (BuiltinType BuiltinFloat32) = Size32
fieldSize (BuiltinType BuiltinFloat64) = Size64
fieldSize (BuiltinType BuiltinText) = SizeReference
fieldSize (BuiltinType BuiltinData) = SizeReference
fieldSize (EnumType _) = Size16  -- TODO: ??
fieldSize (StructType _) = SizeReference
fieldSize (InterfaceType _) = SizeReference
fieldSize (ListType _) = SizeReference

fieldValueSize VoidDesc = Size0
fieldValueSize (BoolDesc _) = Size1
fieldValueSize (Int8Desc _) = Size8
fieldValueSize (Int16Desc _) = Size16
fieldValueSize (Int32Desc _) = Size32
fieldValueSize (Int64Desc _) = Size64
fieldValueSize (UInt8Desc _) = Size8
fieldValueSize (UInt16Desc _) = Size16
fieldValueSize (UInt32Desc _) = Size32
fieldValueSize (UInt64Desc _) = Size64
fieldValueSize (Float32Desc _) = Size32
fieldValueSize (Float64Desc _) = Size64
fieldValueSize (TextDesc _) = SizeReference
fieldValueSize (DataDesc _) = SizeReference
fieldValueSize (EnumValueValueDesc _) = Size16
fieldValueSize (StructValueDesc _) = SizeReference
fieldValueSize (ListDesc _) = SizeReference

elementSize (StructType StructDesc { structPacking =
        PackingState { packingDataSize = ds, packingReferenceCount = rc } }) =
    SizeInlineComposite ds rc
elementSize t = fieldSize t

sizeInBits Size0 = 0
sizeInBits Size1 = 1
sizeInBits Size8 = 8
sizeInBits Size16 = 16
sizeInBits Size32 = 32
sizeInBits Size64 = 64
sizeInBits SizeReference = 64
sizeInBits (SizeInlineComposite d r) = (d + r) * 64

-- Render the type descriptor's name as a string, appropriate for use in the given scope.
typeName :: Desc -> TypeDesc -> String
typeName _ (BuiltinType t) = builtinTypeName t  -- TODO:  Check for shadowing.
typeName scope (EnumType desc) = descQualifiedName scope (DescEnum desc)
typeName scope (StructType desc) = descQualifiedName scope (DescStruct desc)
typeName scope (InterfaceType desc) = descQualifiedName scope (DescInterface desc)
typeName scope (ListType t) = "List(" ++ typeName scope t ++ ")"

-- Computes the qualified name for the given descriptor within the given scope.
-- At present the scope is only used to determine whether the target is in the same file.  If
-- not, an "import" expression is used.
-- This could be made fancier in a couple ways:
-- 1) Drop the common prefix between scope and desc to form a minimal relative name.  Note that
--    we'll need to check for shadowing.
-- 2) Examine aliases visible in the current scope to see if they refer to a prefix of the target
--    symbol, and use them if so.  A particularly important case of this is imports -- typically
--    the import will have an alias in the file scope.
descQualifiedName :: Desc -> Desc -> String
descQualifiedName (DescFile scope) (DescFile desc) =
    if fileName scope == fileName desc
        then ""
        else printf "import \"%s\"" (fileName desc)
descQualifiedName (DescFile scope) desc = printf "%s.%s" parent (descName desc) where
    parent = descQualifiedName (DescFile scope) (descParent desc)
descQualifiedName scope desc = descQualifiedName (descParent scope) desc

data FileDesc = FileDesc
    { fileName :: String
    , fileImports :: [FileDesc]
    , fileAliases :: [AliasDesc]
    , fileConstants :: [ConstantDesc]
    , fileEnums :: [EnumDesc]
    , fileStructs :: [StructDesc]
    , fileInterfaces :: [InterfaceDesc]
    , fileOptions :: OptionMap
    , fileMemberMap :: MemberMap
    , fileImportMap :: Map.Map String FileDesc
    , fileStatements :: [CompiledStatement]
    }

data AliasDesc = AliasDesc
    { aliasName :: String
    , aliasParent :: Desc
    , aliasTarget :: Desc
    }

data ConstantDesc = ConstantDesc
    { constantName :: String
    , constantParent :: Desc
    , constantType :: TypeDesc
    , constantValue :: ValueDesc
    }

data EnumDesc = EnumDesc
    { enumName :: String
    , enumParent :: Desc
    , enumValues :: [EnumValueDesc]
    , enumOptions :: OptionMap
    , enumMemberMap :: MemberMap
    , enumStatements :: [CompiledStatement]
    }

data EnumValueDesc = EnumValueDesc
    { enumValueName :: String
    , enumValueParent :: EnumDesc
    , enumValueNumber :: Integer
    , enumValueOptions :: OptionMap
    , enumValueStatements :: [CompiledStatement]
    }

data StructDesc = StructDesc
    { structName :: String
    , structParent :: Desc
    , structPacking :: PackingState
    , structFields :: [FieldDesc]
    , structUnions :: [UnionDesc]
    , structNestedAliases :: [AliasDesc]
    , structNestedConstants :: [ConstantDesc]
    , structNestedEnums :: [EnumDesc]
    , structNestedStructs :: [StructDesc]
    , structNestedInterfaces :: [InterfaceDesc]
    , structOptions :: OptionMap
    , structMemberMap :: MemberMap
    , structStatements :: [CompiledStatement]

    -- Don't use these directly, use the members of FieldDesc and UnionDesc.
    -- These fields are exposed here only because I was too lazy to create a way to pass them on
    -- the side when compiling members of a struct.
    , structFieldPackingMap :: Map.Map Integer (Integer, PackingState)
    , structUnionPackingMap :: Map.Map Integer UnionPackingState
    }

data UnionDesc = UnionDesc
    { unionName :: String
    , unionParent :: StructDesc
    , unionNumber :: Integer
    , unionTagOffset :: Integer
    , unionTagPacking :: PackingState
    , unionDataOffset :: Maybe Integer
    , unionReferenceOffset :: Maybe Integer
    , unionRetroactiveSlot :: Maybe (Integer, FieldSize)
    , unionFields :: [FieldDesc]
    , unionOptions :: OptionMap
    , unionStatements :: [CompiledStatement]
    }

data FieldDesc = FieldDesc
    { fieldName :: String
    , fieldParent :: StructDesc
    , fieldNumber :: Integer
    , fieldOffset :: Integer
    , fieldPacking :: PackingState    -- PackingState for the struct *if* this were the final field.
    , fieldUnion :: Maybe UnionDesc
    , fieldType :: TypeDesc
    , fieldDefaultValue :: Maybe ValueDesc
    , fieldOptions :: OptionMap
    , fieldStatements :: [CompiledStatement]
    }

data InterfaceDesc = InterfaceDesc
    { interfaceName :: String
    , interfaceParent :: Desc
    , interfaceMethods :: [MethodDesc]
    , interfaceNestedAliases :: [AliasDesc]
    , interfaceNestedConstants :: [ConstantDesc]
    , interfaceNestedEnums :: [EnumDesc]
    , interfaceNestedStructs :: [StructDesc]
    , interfaceNestedInterfaces :: [InterfaceDesc]
    , interfaceOptions :: OptionMap
    , interfaceMemberMap :: MemberMap
    , interfaceStatements :: [CompiledStatement]
    }

data MethodDesc = MethodDesc
    { methodName :: String
    , methodParent :: InterfaceDesc
    , methodNumber :: Integer
    , methodParams :: [(String, TypeDesc, Maybe ValueDesc)]
    , methodReturnType :: TypeDesc
    , methodOptions :: OptionMap
    , methodStatements :: [CompiledStatement]
    }

type OptionMap = Map.Map String OptionAssignmentDesc

data OptionAssignmentDesc = OptionAssignmentDesc
    { optionAssignmentParent :: Desc
    , optionAssignmentOption :: OptionDesc
    , optionAssignmentValue :: ValueDesc
    }

data OptionDesc = OptionDesc
    { optionName :: String
    , optionParent :: Desc
    , optionId :: String
    , optionType :: TypeDesc
    , optionDefaultValue :: Maybe ValueDesc
    }

data CompiledStatement = CompiledMember Desc
                       | CompiledOption OptionAssignmentDesc

-- TODO:  Print options as well as members.  Will be ugly-ish.
descToCode :: String -> Desc -> String
descToCode indent (DescFile desc) = concatMap (statementToCode indent) (fileStatements desc)
descToCode indent (DescAlias desc) = printf "%susing %s = %s;\n" indent
    (aliasName desc)
    (descQualifiedName (aliasParent desc) (aliasTarget desc))
descToCode indent (DescConstant desc) = printf "%sconst %s: %s = %s;\n" indent
    (constantName desc)
    (typeName (constantParent desc) (constantType desc))
    (valueString (constantValue desc))
descToCode indent (DescEnum desc) = printf "%senum %s%s" indent
    (enumName desc)
    (blockCode indent (enumStatements desc))
descToCode indent (DescEnumValue desc) = printf "%s%s = %d%s" indent
    (enumValueName desc) (enumValueNumber desc) (maybeBlockCode indent $ enumValueStatements desc)
descToCode indent (DescStruct desc) = printf "%sstruct %s%s" indent
    (structName desc)
    (blockCode indent (structStatements desc))
descToCode indent (DescField desc) = printf "%s%s@%d%s: %s%s;  # %s\n" indent
    (fieldName desc) (fieldNumber desc)
    (case fieldUnion desc of { Nothing -> ""; Just u -> " in " ++ unionName u})
    (typeName (DescStruct (fieldParent desc)) (fieldType desc))
    (case fieldDefaultValue desc of { Nothing -> ""; Just v -> " = " ++ valueString v; })
    (case fieldSize $ fieldType desc of
        SizeReference -> printf "ref[%d]" $ fieldOffset desc
        SizeInlineComposite _ _ -> "??"
        s -> let
            bits = (sizeInBits s)
            offset = fieldOffset desc
            in printf "bits[%d, %d)" (offset * bits) ((offset + 1) * bits))
--    (maybeBlockCode indent $ fieldStatements desc)
descToCode indent (DescUnion desc) = printf "%sunion %s@%d;  # [%d, %d)\n" indent
    (unionName desc) (unionNumber desc)
    (unionTagOffset desc * 8) (unionTagOffset desc * 8 + 8)
--    (maybeBlockCode indent $ unionStatements desc)
descToCode indent (DescInterface desc) = printf "%sinterface %s%s" indent
    (interfaceName desc)
    (blockCode indent (interfaceStatements desc))
descToCode indent (DescMethod desc) = printf "%s%s@%d(%s): %s%s" indent
    (methodName desc) (methodNumber desc)
    (delimit ", " (map paramToCode (methodParams desc)))
    (typeName scope (methodReturnType desc))
    (maybeBlockCode indent $ methodStatements desc) where
        scope = DescInterface (methodParent desc)
        paramToCode (name, t, Nothing) = printf "%s: %s" name (typeName scope t)
        paramToCode (name, t, Just v) = printf "%s: %s = %s"
            name (typeName scope t) (valueString v)
descToCode _ (DescOption _) = error "options not implemented"
descToCode _ (DescBuiltinType _) = error "Can't print code for builtin type."
descToCode _ DescBuiltinList = error "Can't print code for builtin type."

statementToCode :: String -> CompiledStatement -> String
statementToCode indent (CompiledMember desc) = descToCode indent desc
statementToCode indent (CompiledOption desc) = printf "%s%s.%s = %s;\n" indent
    (descQualifiedName (optionAssignmentParent desc) $ optionParent $ optionAssignmentOption desc)
    (optionName $ optionAssignmentOption desc)
    (valueString (optionAssignmentValue desc))

maybeBlockCode :: String -> [CompiledStatement] -> String
maybeBlockCode _ [] = ";\n"
maybeBlockCode indent statements = blockCode indent statements

blockCode :: String -> [CompiledStatement] -> String
blockCode indent statements = printf " {\n%s%s}\n"
    (concatMap (statementToCode ("  " ++ indent)) statements)
    indent

instance Show FileDesc where { show desc = descToCode "" (DescFile desc) }
instance Show AliasDesc where { show desc = descToCode "" (DescAlias desc) }
instance Show ConstantDesc where { show desc = descToCode "" (DescConstant desc) }
instance Show EnumDesc where { show desc = descToCode "" (DescEnum desc) }
instance Show EnumValueDesc where { show desc = descToCode "" (DescEnumValue desc) }
instance Show StructDesc where { show desc = descToCode "" (DescStruct desc) }
instance Show FieldDesc where { show desc = descToCode "" (DescField desc) }
instance Show InterfaceDesc where { show desc = descToCode "" (DescInterface desc) }
instance Show MethodDesc where { show desc = descToCode "" (DescMethod desc) }
