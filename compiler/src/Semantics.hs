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
import qualified Data.Set as Set
import qualified Data.List as List
import qualified Data.Maybe as Maybe
import Data.Int (Int8, Int16, Int32, Int64)
import Data.Word (Word8, Word16, Word32, Word64)
import Data.Char (chr)
import Text.Printf(printf)
import Control.Monad(join)
import Util(delimit)
import Grammar(AnnotationTarget(..))

-- Field counts are 16-bit, therefore there cannot be more than 65535 fields, therefore the max
-- ordinal is 65534.
maxOrdinal = 65534 :: Integer

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
          | DescParam ParamDesc
          | DescAnnotation AnnotationDesc
          | DescBuiltinType BuiltinType
          | DescBuiltinList
          | DescBuiltinId

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
descName (DescParam     d) = paramName d
descName (DescAnnotation d) = annotationName d
descName (DescBuiltinType d) = builtinTypeName d
descName DescBuiltinList = "List"
descName DescBuiltinId = "id"

descId (DescFile      d) = fileId d
descId (DescAlias     _) = Nothing
descId (DescConstant  d) = constantId d
descId (DescEnum      d) = enumId d
descId (DescEnumValue d) = enumValueId d
descId (DescStruct    d) = structId d
descId (DescUnion     d) = unionId d
descId (DescField     d) = fieldId d
descId (DescInterface d) = interfaceId d
descId (DescMethod    d) = methodId d
descId (DescParam     d) = paramId d
descId (DescAnnotation d) = annotationId d
descId (DescBuiltinType _) = Nothing
descId DescBuiltinList = Nothing
descId DescBuiltinId = Just "0U0T3e_SnatEfk6UcH2tcjTt1E0"

-- Gets the ID if explicitly defined, or generates it by appending ".name" to the parent's ID.
-- If no ancestor has an ID, still returns Nothing.
descAutoId d = case descId d of
    Just i -> Just i
    Nothing -> case d of
        DescFile _ -> Nothing
        _ -> fmap (++ '.':descName d) $ descAutoId $ descParent d

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
descParent (DescParam     d) = DescMethod (paramParent d)
descParent (DescAnnotation d) = annotationParent d
descParent (DescBuiltinType _) = error "Builtin type has no parent."
descParent DescBuiltinList = error "Builtin type has no parent."
descParent DescBuiltinId = error "Builtin annotation has no parent."

descAnnotations (DescFile      d) = fileAnnotations d
descAnnotations (DescAlias     _) = Map.empty
descAnnotations (DescConstant  d) = constantAnnotations d
descAnnotations (DescEnum      d) = enumAnnotations d
descAnnotations (DescEnumValue d) = enumValueAnnotations d
descAnnotations (DescStruct    d) = structAnnotations d
descAnnotations (DescUnion     d) = unionAnnotations d
descAnnotations (DescField     d) = fieldAnnotations d
descAnnotations (DescInterface d) = interfaceAnnotations d
descAnnotations (DescMethod    d) = methodAnnotations d
descAnnotations (DescParam     d) = paramAnnotations d
descAnnotations (DescAnnotation d) = annotationAnnotations d
descAnnotations (DescBuiltinType _) = Map.empty
descAnnotations DescBuiltinList = Map.empty
descAnnotations DescBuiltinId = Map.empty

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

valueString VoidDesc = "void"
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
    assignmentString (field, value) = case fieldUnion field of
        Nothing -> fieldName field ++ " = " ++ valueString value
        Just (u, _) -> unionName u ++ " = " ++ fieldName field ++
            (case value of
                StructValueDesc _ -> valueString value
                _ -> "(" ++ valueString value ++ ")")
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

packingSize PackingState { packingDataSize = ds, packingReferenceCount = rc } = ds + rc

-- Represents the current packing state of a union.  The parameters are:
-- - The offset of a 64-bit word in the data segment allocated to the union.
-- - The offset of a reference allocated to the union.
-- - The offset of a smaller piece of the data segment allocated to the union.  Such a smaller
--   piece exists if one field in the union has lower number than the union itself -- in this case,
--   this is the piece that had been allocated to that field, and is now retroactively part of the
--   union.
data UnionPackingState = UnionPackingState
    { unionPackDataOffset :: Maybe (Integer, FieldSize)
    , unionPackReferenceOffset :: Maybe Integer
    }

data FieldSize = Size0 | Size1 | Size8 | Size16 | Size32 | Size64 | SizeReference
               | SizeInlineComposite Integer Integer

isDataFieldSize SizeReference = False
isDataFieldSize (SizeInlineComposite _ _) = False
isDataFieldSize _ = True

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
    , fileId :: Maybe String
    , fileImports :: [FileDesc]
    , fileAliases :: [AliasDesc]
    , fileConstants :: [ConstantDesc]
    , fileEnums :: [EnumDesc]
    , fileStructs :: [StructDesc]
    , fileInterfaces :: [InterfaceDesc]
    , fileAnnotations :: AnnotationMap
    , fileMemberMap :: MemberMap
    , fileImportMap :: Map.Map String FileDesc
    , fileStatements :: [Desc]
    }

data AliasDesc = AliasDesc
    { aliasName :: String
    , aliasParent :: Desc
    , aliasTarget :: Desc
    }

data ConstantDesc = ConstantDesc
    { constantName :: String
    , constantId :: Maybe String
    , constantParent :: Desc
    , constantType :: TypeDesc
    , constantAnnotations :: AnnotationMap
    , constantValue :: ValueDesc
    }

data EnumDesc = EnumDesc
    { enumName :: String
    , enumId :: Maybe String
    , enumParent :: Desc
    , enumValues :: [EnumValueDesc]
    , enumAnnotations :: AnnotationMap
    , enumMemberMap :: MemberMap
    , enumStatements :: [Desc]
    }

data EnumValueDesc = EnumValueDesc
    { enumValueName :: String
    , enumValueId :: Maybe String
    , enumValueParent :: EnumDesc
    , enumValueNumber :: Integer
    , enumValueAnnotations :: AnnotationMap
    }

data StructDesc = StructDesc
    { structName :: String
    , structId :: Maybe String
    , structParent :: Desc
    , structPacking :: PackingState
    , structFields :: [FieldDesc]
    , structUnions :: [UnionDesc]
    , structNestedAliases :: [AliasDesc]
    , structNestedConstants :: [ConstantDesc]
    , structNestedEnums :: [EnumDesc]
    , structNestedStructs :: [StructDesc]
    , structNestedInterfaces :: [InterfaceDesc]
    , structAnnotations :: AnnotationMap
    , structMemberMap :: MemberMap
    , structStatements :: [Desc]

    -- Don't use this directly, use the members of FieldDesc and UnionDesc.
    -- This field is exposed here only because I was too lazy to create a way to pass it on
    -- the side when compiling members of a struct.
    , structFieldPackingMap :: Map.Map Integer (Integer, PackingState)
    }

data UnionDesc = UnionDesc
    { unionName :: String
    , unionId :: Maybe String
    , unionParent :: StructDesc
    , unionNumber :: Integer
    , unionTagOffset :: Integer
    , unionTagPacking :: PackingState
    , unionFields :: [FieldDesc]
    , unionAnnotations :: AnnotationMap
    , unionMemberMap :: MemberMap
    , unionStatements :: [Desc]

    -- Maps field numbers to discriminants for all fields in the union.
    , unionFieldDiscriminantMap :: Map.Map Integer Integer
    }

data FieldDesc = FieldDesc
    { fieldName :: String
    , fieldId :: Maybe String
    , fieldParent :: StructDesc
    , fieldNumber :: Integer
    , fieldOffset :: Integer
    , fieldPacking :: PackingState    -- PackingState for the struct *if* this were the final field.
    , fieldUnion :: Maybe (UnionDesc, Integer)  -- Integer is value of union discriminant.
    , fieldType :: TypeDesc
    , fieldDefaultValue :: Maybe ValueDesc
    , fieldAnnotations :: AnnotationMap
    }

data InterfaceDesc = InterfaceDesc
    { interfaceName :: String
    , interfaceId :: Maybe String
    , interfaceParent :: Desc
    , interfaceMethods :: [MethodDesc]
    , interfaceNestedAliases :: [AliasDesc]
    , interfaceNestedConstants :: [ConstantDesc]
    , interfaceNestedEnums :: [EnumDesc]
    , interfaceNestedStructs :: [StructDesc]
    , interfaceNestedInterfaces :: [InterfaceDesc]
    , interfaceAnnotations :: AnnotationMap
    , interfaceMemberMap :: MemberMap
    , interfaceStatements :: [Desc]
    }

data MethodDesc = MethodDesc
    { methodName :: String
    , methodId :: Maybe String
    , methodParent :: InterfaceDesc
    , methodNumber :: Integer
    , methodParams :: [ParamDesc]
    , methodReturnType :: TypeDesc
    , methodAnnotations :: AnnotationMap
    }

data ParamDesc = ParamDesc
    { paramName :: String
    , paramId :: Maybe String
    , paramParent :: MethodDesc
    , paramNumber :: Integer
    , paramType :: TypeDesc
    , paramDefaultValue :: Maybe ValueDesc
    , paramAnnotations :: AnnotationMap
    }

data AnnotationDesc = AnnotationDesc
    { annotationName :: String
    , annotationParent :: Desc
    , annotationType :: TypeDesc
    , annotationAnnotations :: AnnotationMap
    , annotationId :: Maybe String
    , annotationTargets :: Set.Set AnnotationTarget
    }

type AnnotationMap = Map.Map String (AnnotationDesc, ValueDesc)

descToCode :: String -> Desc -> String
descToCode indent self@(DescFile desc) = printf "# %s\n%s%s%s"
    (fileName desc)
    (case fileId desc of
        Just i -> printf "$id(%s);\n" $ show i
        Nothing -> "")
    (concatMap ((++ ";\n") . annotationCode self) $ Map.toList $ fileAnnotations desc)
    (concatMap (descToCode indent) (fileStatements desc))
descToCode indent (DescAlias desc) = printf "%susing %s = %s;\n" indent
    (aliasName desc)
    (descQualifiedName (aliasParent desc) (aliasTarget desc))
descToCode indent self@(DescConstant desc) = printf "%sconst %s: %s = %s%s;\n" indent
    (constantName desc)
    (typeName (descParent self) (constantType desc))
    (valueString (constantValue desc))
    (annotationsCode self)
descToCode indent self@(DescEnum desc) = printf "%senum %s%s {\n%s%s}\n" indent
    (enumName desc)
    (annotationsCode self)
    (blockCode indent (enumStatements desc))
    indent
descToCode indent self@(DescEnumValue desc) = printf "%s%s @%d%s;\n" indent
    (enumValueName desc) (enumValueNumber desc)
    (annotationsCode self)
descToCode indent self@(DescStruct desc) = printf "%sstruct %s%s {\n%s%s}\n" indent
    (structName desc)
    (annotationsCode self)
    (blockCode indent (structStatements desc))
    indent
descToCode indent self@(DescField desc) = printf "%s%s@%d%s: %s%s%s;  # %s\n" indent
    (fieldName desc) (fieldNumber desc)
    (case fieldUnion desc of { Nothing -> ""; Just (u, _) -> " in " ++ unionName u})
    (typeName (descParent self) (fieldType desc))
    (case fieldDefaultValue desc of { Nothing -> ""; Just v -> " = " ++ valueString v; })
    (annotationsCode self)
    (case fieldSize $ fieldType desc of
        SizeReference -> printf "ref[%d]" $ fieldOffset desc
        SizeInlineComposite _ _ -> "??"
        s -> let
            bits = sizeInBits s
            offset = fieldOffset desc
            in printf "bits[%d, %d)" (offset * bits) ((offset + 1) * bits))
descToCode indent self@(DescUnion desc) = printf "%sunion %s@%d%s {  # [%d, %d)\n%s%s}\n" indent
    (unionName desc) (unionNumber desc)
    (annotationsCode self)
    (unionTagOffset desc * 16) (unionTagOffset desc * 16 + 16)
    (blockCode indent $ unionStatements desc)
    indent
descToCode indent self@(DescInterface desc) = printf "%sinterface %s%s {\n%s%s}\n" indent
    (interfaceName desc)
    (annotationsCode self)
    (blockCode indent (interfaceStatements desc))
    indent
descToCode indent self@(DescMethod desc) = printf "%s%s@%d(%s): %s%s" indent
    (methodName desc) (methodNumber desc)
    (delimit ", " (map (descToCode indent . DescParam) (methodParams desc)))
    (typeName (descParent self) (methodReturnType desc))
    (annotationsCode self)
descToCode _ self@(DescParam desc) = printf "%s: %s%s%s"
    (paramName desc)
    (typeName (descParent self) (paramType desc))
    (case paramDefaultValue desc of
        Just v -> printf " = %s" $ valueString v
        Nothing -> "")
    (annotationsCode self)
descToCode indent self@(DescAnnotation desc) = printf "%sannotation %s: %s on(%s)%s;\n" indent
    (annotationName desc)
    (typeName (descParent self) (annotationType desc))
    (delimit ", " $ map show $ Set.toList $ annotationTargets desc)
    (annotationsCode self)
descToCode _ (DescBuiltinType _) = error "Can't print code for builtin type."
descToCode _ DescBuiltinList = error "Can't print code for builtin type."
descToCode _ DescBuiltinId = error "Can't print code for builtin annotation."

maybeBlockCode :: String -> [Desc] -> String
maybeBlockCode _ [] = ";\n"
maybeBlockCode indent statements = printf " {\n%s%s}\n" (blockCode indent statements) indent

blockCode :: String -> [Desc] -> String
blockCode indent = concatMap (descToCode ("  " ++ indent))

annotationCode :: Desc -> (String, (AnnotationDesc, ValueDesc)) -> String
annotationCode scope (_, (desc, VoidDesc)) =
    printf "$%s" (descQualifiedName scope (DescAnnotation desc))
annotationCode scope (_, (desc, val)) =
    printf "$%s(%s)" (descQualifiedName scope (DescAnnotation desc)) (valueString val)

annotationsCode desc = let
    nonIds = concatMap ((' ':) . annotationCode (descParent desc)) $ Map.toList
           $ descAnnotations desc
    in case descId desc of
        Just i -> printf " $id(%s)%s" (show i) nonIds
        Nothing -> nonIds

instance Show FileDesc where { show desc = descToCode "" (DescFile desc) }
instance Show AliasDesc where { show desc = descToCode "" (DescAlias desc) }
instance Show ConstantDesc where { show desc = descToCode "" (DescConstant desc) }
instance Show EnumDesc where { show desc = descToCode "" (DescEnum desc) }
instance Show EnumValueDesc where { show desc = descToCode "" (DescEnumValue desc) }
instance Show StructDesc where { show desc = descToCode "" (DescStruct desc) }
instance Show FieldDesc where { show desc = descToCode "" (DescField desc) }
instance Show InterfaceDesc where { show desc = descToCode "" (DescInterface desc) }
instance Show MethodDesc where { show desc = descToCode "" (DescMethod desc) }
instance Show ParamDesc where { show desc = descToCode "" (DescParam desc) }
instance Show AnnotationDesc where { show desc = descToCode "" (DescAnnotation desc) }
