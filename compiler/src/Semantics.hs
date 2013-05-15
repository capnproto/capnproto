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

-- Inline fields can be 64 words.  (This limit is relied upon by implementations which may need
-- to produce some sort of default value when an inlined field is not actually present in the
-- struct.)
maxInlineFieldBits = 64 * 64 :: Integer

maxStructDataWords = 65536 :: Integer
maxStructPointers = 65536 :: Integer

type ByteString = [Word8]

data Desc = DescFile FileDesc
          | DescUsing UsingDesc
          | DescConstant ConstantDesc
          | DescEnum EnumDesc
          | DescEnumerant EnumerantDesc
          | DescStruct StructDesc
          | DescUnion UnionDesc
          | DescField FieldDesc
          | DescInterface InterfaceDesc
          | DescMethod MethodDesc
          | DescParam ParamDesc
          | DescAnnotation AnnotationDesc
          | DescBuiltinType BuiltinType
          | DescBuiltinList
          | DescBuiltinInline
          | DescBuiltinInlineList
          | DescBuiltinInlineData

descName (DescFile      _) = "(top-level)"
descName (DescUsing     d) = usingName d
descName (DescConstant  d) = constantName d
descName (DescEnum      d) = enumName d
descName (DescEnumerant d) = enumerantName d
descName (DescStruct    d) = structName d
descName (DescUnion     d) = unionName d
descName (DescField     d) = fieldName d
descName (DescInterface d) = interfaceName d
descName (DescMethod    d) = methodName d
descName (DescParam     d) = paramName d
descName (DescAnnotation d) = annotationName d
descName (DescBuiltinType d) = builtinTypeName d
descName DescBuiltinList = "List"
descName DescBuiltinInline = "Inline"
descName DescBuiltinInlineList = "InlineList"
descName DescBuiltinInlineData = "InlineData"

descId (DescFile      d) = fileId d
descId (DescEnum      d) = enumId d
descId (DescStruct    d) = structId d
descId (DescInterface d) = interfaceId d
descId (DescConstant  d) = constantId d
descId (DescAnnotation d) = annotationId d
descId _ = error "This construct does not have an ID."

descParent (DescFile      _) = error "File descriptor has no parent."
descParent (DescUsing     d) = usingParent d
descParent (DescConstant  d) = constantParent d
descParent (DescEnum      d) = enumParent d
descParent (DescEnumerant d) = DescEnum (enumerantParent d)
descParent (DescStruct    d) = structParent d
descParent (DescUnion     d) = DescStruct (unionParent d)
descParent (DescField     d) = DescStruct (fieldParent d)
descParent (DescInterface d) = interfaceParent d
descParent (DescMethod    d) = DescInterface (methodParent d)
descParent (DescParam     d) = DescMethod (paramParent d)
descParent (DescAnnotation d) = annotationParent d
descParent (DescBuiltinType _) = error "Builtin type has no parent."
descParent DescBuiltinList = error "Builtin type has no parent."
descParent DescBuiltinInline = error "Builtin type has no parent."
descParent DescBuiltinInlineList = error "Builtin type has no parent."
descParent DescBuiltinInlineData = error "Builtin type has no parent."

descFile (DescFile d) = d
descFile desc = descFile $ descParent desc

descAnnotations (DescFile      d) = fileAnnotations d
descAnnotations (DescUsing     _) = Map.empty
descAnnotations (DescConstant  d) = constantAnnotations d
descAnnotations (DescEnum      d) = enumAnnotations d
descAnnotations (DescEnumerant d) = enumerantAnnotations d
descAnnotations (DescStruct    d) = structAnnotations d
descAnnotations (DescUnion     d) = unionAnnotations d
descAnnotations (DescField     d) = fieldAnnotations d
descAnnotations (DescInterface d) = interfaceAnnotations d
descAnnotations (DescMethod    d) = methodAnnotations d
descAnnotations (DescParam     d) = paramAnnotations d
descAnnotations (DescAnnotation d) = annotationAnnotations d
descAnnotations (DescBuiltinType _) = Map.empty
descAnnotations DescBuiltinList = Map.empty
descAnnotations DescBuiltinInline = Map.empty
descAnnotations DescBuiltinInlineList = Map.empty
descAnnotations DescBuiltinInlineData = Map.empty

descRuntimeImports (DescFile      _) = error "Not to be called on files."
descRuntimeImports (DescUsing     d) = usingRuntimeImports d
descRuntimeImports (DescConstant  d) = constantRuntimeImports d
descRuntimeImports (DescEnum      d) = enumRuntimeImports d
descRuntimeImports (DescEnumerant d) = enumerantRuntimeImports d
descRuntimeImports (DescStruct    d) = structRuntimeImports d
descRuntimeImports (DescUnion     d) = unionRuntimeImports d
descRuntimeImports (DescField     d) = fieldRuntimeImports d
descRuntimeImports (DescInterface d) = interfaceRuntimeImports d
descRuntimeImports (DescMethod    d) = methodRuntimeImports d
descRuntimeImports (DescParam     d) = paramRuntimeImports d
descRuntimeImports (DescAnnotation d) = annotationRuntimeImports d
descRuntimeImports (DescBuiltinType _) = []
descRuntimeImports DescBuiltinList = []
descRuntimeImports DescBuiltinInline = []
descRuntimeImports DescBuiltinInlineList = []
descRuntimeImports DescBuiltinInlineData = []

type MemberMap = Map.Map String (Maybe Desc)

lookupMember :: String -> MemberMap -> Maybe Desc
lookupMember name members = join (Map.lookup name members)

data BuiltinType = BuiltinVoid | BuiltinBool
                 | BuiltinInt8 | BuiltinInt16 | BuiltinInt32 | BuiltinInt64
                 | BuiltinUInt8 | BuiltinUInt16 | BuiltinUInt32 | BuiltinUInt64
                 | BuiltinFloat32 | BuiltinFloat64
                 | BuiltinText | BuiltinData
                 | BuiltinObject
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
               | EnumerantValueDesc EnumerantDesc
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
valueString (EnumerantValueDesc v) = enumerantName v
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
              | InlineStructType StructDesc
              | InterfaceType InterfaceDesc
              | ListType TypeDesc
              | InlineListType TypeDesc Integer
              | InlineDataType Integer

typeRuntimeImports (BuiltinType _) = []
typeRuntimeImports (EnumType d) = [descFile (DescEnum d)]
typeRuntimeImports (StructType d) = [descFile (DescStruct d)]
typeRuntimeImports (InlineStructType d) = [descFile (DescStruct d)]
typeRuntimeImports (InterfaceType d) = [descFile (DescInterface d)]
typeRuntimeImports (ListType d) = typeRuntimeImports d
typeRuntimeImports (InlineListType d _) = typeRuntimeImports d
typeRuntimeImports (InlineDataType _) = []

data DataSectionSize = DataSection1 | DataSection8 | DataSection16 | DataSection32
                     | DataSectionWords Integer

dataSectionWordSize ds = case ds of
    DataSectionWords w -> w
    _ -> 1

dataSectionAlignment DataSection1 = Size1
dataSectionAlignment DataSection8 = Size8
dataSectionAlignment DataSection16 = Size16
dataSectionAlignment DataSection32 = Size32
dataSectionAlignment (DataSectionWords _) = Size64

dataSectionBits DataSection1 = 1
dataSectionBits DataSection8 = 8
dataSectionBits DataSection16 = 16
dataSectionBits DataSection32 = 32
dataSectionBits (DataSectionWords w) = w * 64

dataSizeToSectionSize Size1 = DataSection1
dataSizeToSectionSize Size8 = DataSection8
dataSizeToSectionSize Size16 = DataSection16
dataSizeToSectionSize Size32 = DataSection32
dataSizeToSectionSize Size64 = DataSectionWords 1

dataSectionSizeString DataSection1 = error "Data section for display can't be 1 bit."
dataSectionSizeString DataSection8 = "1 bytes"
dataSectionSizeString DataSection16 = "2 bytes"
dataSectionSizeString DataSection32 = "4 bytes"
dataSectionSizeString (DataSectionWords n) = show (n * 8) ++ " bytes"

data DataSize = Size1 | Size8 | Size16 | Size32 | Size64 deriving(Eq, Ord, Enum)

dataSizeInBits :: DataSize -> Integer
dataSizeInBits Size1 = 1
dataSizeInBits Size8 = 8
dataSizeInBits Size16 = 16
dataSizeInBits Size32 = 32
dataSizeInBits Size64 = 64

data FieldSize = SizeVoid
               | SizeData DataSize
               | SizePointer
               | SizeInlineComposite DataSectionSize Integer

fieldSizeInBits SizeVoid = 0
fieldSizeInBits (SizeData d) = dataSizeInBits d
fieldSizeInBits SizePointer = 64
fieldSizeInBits (SizeInlineComposite ds pc) = dataSectionBits ds + pc * 64

data FieldOffset = VoidOffset
                 | DataOffset DataSize Integer
                 | PointerOffset Integer
                 | InlineCompositeOffset
                     { inlineCompositeDataOffset :: Integer
                     , inlineCompositePointerOffset :: Integer
                     , inlineCompositeDataSize :: DataSectionSize
                     , inlineCompositePointerSize :: Integer
                     }

offsetToSize :: FieldOffset -> FieldSize
offsetToSize VoidOffset = SizeVoid
offsetToSize (DataOffset s _) = SizeData s
offsetToSize (PointerOffset _) = SizePointer
offsetToSize (InlineCompositeOffset _ _ d p) = SizeInlineComposite d p

fieldSize (BuiltinType BuiltinVoid) = SizeVoid
fieldSize (BuiltinType BuiltinBool) = SizeData Size1
fieldSize (BuiltinType BuiltinInt8) = SizeData Size8
fieldSize (BuiltinType BuiltinInt16) = SizeData Size16
fieldSize (BuiltinType BuiltinInt32) = SizeData Size32
fieldSize (BuiltinType BuiltinInt64) = SizeData Size64
fieldSize (BuiltinType BuiltinUInt8) = SizeData Size8
fieldSize (BuiltinType BuiltinUInt16) = SizeData Size16
fieldSize (BuiltinType BuiltinUInt32) = SizeData Size32
fieldSize (BuiltinType BuiltinUInt64) = SizeData Size64
fieldSize (BuiltinType BuiltinFloat32) = SizeData Size32
fieldSize (BuiltinType BuiltinFloat64) = SizeData Size64
fieldSize (BuiltinType BuiltinText) = SizePointer
fieldSize (BuiltinType BuiltinData) = SizePointer
fieldSize (BuiltinType BuiltinObject) = SizePointer
fieldSize (EnumType _) = SizeData Size16
fieldSize (StructType _) = SizePointer
fieldSize (InlineStructType StructDesc { structDataSize = ds, structPointerCount = ps }) =
    SizeInlineComposite ds ps
fieldSize (InterfaceType _) = SizePointer
fieldSize (ListType _) = SizePointer
fieldSize (InlineListType element size) = let
    minDataSectionForBits bits
        | bits <= 0 = DataSectionWords 0
        | bits <= 1 = DataSection1
        | bits <= 8 = DataSection8
        | bits <= 16 = DataSection16
        | bits <= 32 = DataSection32
        | otherwise = DataSectionWords $ div (bits + 63) 64
    dataSection = case fieldSize element of
        SizeVoid -> DataSectionWords 0
        SizeData s -> minDataSectionForBits $ dataSizeInBits s * size
        SizePointer -> DataSectionWords 0
        SizeInlineComposite ds _ -> minDataSectionForBits $ dataSectionBits ds * size
    pointerCount = case fieldSize element of
        SizeVoid -> 0
        SizeData _ -> 0
        SizePointer -> size
        SizeInlineComposite _ pc -> pc * size
    in SizeInlineComposite dataSection pointerCount
fieldSize (InlineDataType size)
    | size <= 0 = SizeInlineComposite (DataSectionWords 0) 0
    | size <= 1 = SizeInlineComposite DataSection8 0
    | size <= 2 = SizeInlineComposite DataSection16 0
    | size <= 4 = SizeInlineComposite DataSection32 0
    | otherwise = SizeInlineComposite (DataSectionWords (div (size + 7) 8)) 0

-- Render the type descriptor's name as a string, appropriate for use in the given scope.
typeName :: Desc -> TypeDesc -> String
typeName _ (BuiltinType t) = builtinTypeName t  -- TODO:  Check for shadowing.
typeName scope (EnumType desc) = descQualifiedName scope (DescEnum desc)
typeName scope (StructType desc) = descQualifiedName scope (DescStruct desc)
typeName scope (InlineStructType desc) = descQualifiedName scope (DescStruct desc)
typeName scope (InterfaceType desc) = descQualifiedName scope (DescInterface desc)
typeName scope (ListType t) = "List(" ++ typeName scope t ++ ")"
typeName scope (InlineListType t s) = printf "InlineList(%s, %d)" (typeName scope t) s
typeName _ (InlineDataType s) = printf "InlineData(%d)" s

-- Computes the qualified name for the given descriptor within the given scope.
-- At present the scope is only used to determine whether the target is in the same file.  If
-- not, an "import" expression is used.
-- This could be made fancier in a couple ways:
-- 1) Drop the common prefix between scope and desc to form a minimal relative name.  Note that
--    we'll need to check for shadowing.
-- 2) Examine `using`s visible in the current scope to see if they refer to a prefix of the target
--    symbol, and use them if so.  A particularly important case of this is imports -- typically
--    the import will have a `using` in the file scope.
descQualifiedName :: Desc -> Desc -> String

-- Builtin descs can be aliased with "using", so we need to support them.
descQualifiedName _ (DescBuiltinType t) = builtinTypeName t
descQualifiedName _ DescBuiltinList = "List"
descQualifiedName _ DescBuiltinInline = "Inline"
descQualifiedName _ DescBuiltinInlineList = "InlineList"
descQualifiedName _ DescBuiltinInlineData = "InlineData"

descQualifiedName (DescFile scope) (DescFile desc) =
    if fileName scope == fileName desc
        then ""
        else printf "import \"%s\"" (fileName desc)
descQualifiedName (DescFile scope) desc = printf "%s.%s" parent (descName desc) where
    parent = descQualifiedName (DescFile scope) (descParent desc)
descQualifiedName scope desc = descQualifiedName (descParent scope) desc

data FileDesc = FileDesc
    { fileName :: String
    , fileId :: Word64
    , fileImports :: [FileDesc]
    -- Set of imports which are used at runtime, i.e. not just for annotations.
    -- The set contains file names matching files in fileImports.
    , fileRuntimeImports :: Set.Set String
    , fileAnnotations :: AnnotationMap
    , fileMemberMap :: MemberMap
    , fileImportMap :: Map.Map String FileDesc
    , fileMembers :: [Desc]
    }

data UsingDesc = UsingDesc
    { usingName :: String
    , usingParent :: Desc
    , usingTarget :: Desc
    }

usingRuntimeImports _ = []

data ConstantDesc = ConstantDesc
    { constantName :: String
    , constantId :: Word64
    , constantParent :: Desc
    , constantType :: TypeDesc
    , constantAnnotations :: AnnotationMap
    , constantValue :: ValueDesc
    }

constantRuntimeImports desc = typeRuntimeImports $ constantType desc

data EnumDesc = EnumDesc
    { enumName :: String
    , enumId :: Word64
    , enumParent :: Desc
    , enumerants :: [EnumerantDesc]
    , enumAnnotations :: AnnotationMap
    , enumMemberMap :: MemberMap
    , enumMembers :: [Desc]
    }

enumRuntimeImports desc = concatMap descRuntimeImports $ enumMembers desc

data EnumerantDesc = EnumerantDesc
    { enumerantName :: String
    , enumerantParent :: EnumDesc
    , enumerantNumber :: Integer
    , enumerantAnnotations :: AnnotationMap
    }

enumerantRuntimeImports _ = []

data StructDesc = StructDesc
    { structName :: String
    , structId :: Word64
    , structParent :: Desc
    , structDataSize :: DataSectionSize
    , structPointerCount :: Integer
    , structIsFixedWidth :: Bool
    , structFields :: [FieldDesc]
    , structUnions :: [UnionDesc]
    , structAnnotations :: AnnotationMap
    , structMemberMap :: MemberMap
    , structMembers :: [Desc]

    -- Don't use this directly, use the members of FieldDesc and UnionDesc.
    -- This field is exposed here only because I was too lazy to create a way to pass it on
    -- the side when compiling members of a struct.
    , structFieldPackingMap :: Map.Map Integer FieldOffset
    }

structRuntimeImports desc = concatMap descRuntimeImports $ structMembers desc

data UnionDesc = UnionDesc
    { unionName :: String
    , unionParent :: StructDesc
    , unionNumber :: Integer
    , unionTagOffset :: Integer
    , unionFields :: [FieldDesc]
    , unionAnnotations :: AnnotationMap
    , unionMemberMap :: MemberMap
    , unionMembers :: [Desc]

    -- Maps field numbers to discriminants for all fields in the union.
    , unionFieldDiscriminantMap :: Map.Map Integer Integer
    }

unionRuntimeImports desc = concatMap descRuntimeImports $ unionMembers desc

data FieldDesc = FieldDesc
    { fieldName :: String
    , fieldParent :: StructDesc
    , fieldNumber :: Integer
    , fieldOffset :: FieldOffset
    , fieldUnion :: Maybe (UnionDesc, Integer)  -- Integer is value of union discriminant.
    , fieldType :: TypeDesc
    , fieldDefaultValue :: Maybe ValueDesc
    , fieldAnnotations :: AnnotationMap
    }

fieldRuntimeImports desc = typeRuntimeImports $ fieldType desc

data InterfaceDesc = InterfaceDesc
    { interfaceName :: String
    , interfaceId :: Word64
    , interfaceParent :: Desc
    , interfaceMethods :: [MethodDesc]
    , interfaceAnnotations :: AnnotationMap
    , interfaceMemberMap :: MemberMap
    , interfaceMembers :: [Desc]
    }

interfaceRuntimeImports desc = concatMap descRuntimeImports $ interfaceMembers desc

data MethodDesc = MethodDesc
    { methodName :: String
    , methodParent :: InterfaceDesc
    , methodNumber :: Integer
    , methodParams :: [ParamDesc]
    , methodReturnType :: TypeDesc
    , methodAnnotations :: AnnotationMap
    }

methodRuntimeImports desc = typeRuntimeImports (methodReturnType desc) ++
                            concatMap paramRuntimeImports (methodParams desc)

data ParamDesc = ParamDesc
    { paramName :: String
    , paramParent :: MethodDesc
    , paramNumber :: Integer
    , paramType :: TypeDesc
    , paramDefaultValue :: Maybe ValueDesc
    , paramAnnotations :: AnnotationMap
    }

paramRuntimeImports desc = typeRuntimeImports $ paramType desc

data AnnotationDesc = AnnotationDesc
    { annotationName :: String
    , annotationParent :: Desc
    , annotationType :: TypeDesc
    , annotationAnnotations :: AnnotationMap
    , annotationId :: Word64
    , annotationTargets :: Set.Set AnnotationTarget
    }

annotationRuntimeImports desc = typeRuntimeImports $ annotationType desc

type AnnotationMap = Map.Map Word64 (AnnotationDesc, ValueDesc)

descToCode :: String -> Desc -> String
descToCode indent self@(DescFile desc) = printf "# %s\n@0x%016x;\n%s%s"
    (fileName desc)
    (fileId desc)
    (concatMap ((++ ";\n") . annotationCode self) $ Map.toList $ fileAnnotations desc)
    (concatMap (descToCode indent) (fileMembers desc))
descToCode indent (DescUsing desc) = printf "%susing %s = %s;\n" indent
    (usingName desc)
    (descQualifiedName (usingParent desc) (usingTarget desc))
descToCode indent self@(DescConstant desc) = printf "%sconst %s: %s = %s%s;\n" indent
    (constantName desc)
    (typeName (descParent self) (constantType desc))
    (valueString (constantValue desc))
    (annotationsCode self)
descToCode indent self@(DescEnum desc) = printf "%senum %s @0x%016x%s {\n%s%s}\n" indent
    (enumName desc)
    (enumId desc)
    (annotationsCode self)
    (blockCode indent (enumMembers desc))
    indent
descToCode indent self@(DescEnumerant desc) = printf "%s%s @%d%s;\n" indent
    (enumerantName desc) (enumerantNumber desc)
    (annotationsCode self)
descToCode indent self@(DescStruct desc) =
    printf "%sstruct %s @0x%016x%s%s {  # %d bytes, %d pointers\n%s%s}\n" indent
        (structName desc)
        (structId desc)
        (if structIsFixedWidth desc
            then printf " fixed(%s, %d pointers) "
                (dataSectionSizeString $ structDataSize desc)
                (structPointerCount desc)
            else "")
        (annotationsCode self)
        (div (dataSectionBits $ structDataSize desc) 8)
        (structPointerCount desc)
        (blockCode indent (structMembers desc))
        indent
descToCode indent self@(DescField desc) = printf "%s%s@%d: %s%s%s;  # %s%s\n" indent
    (fieldName desc) (fieldNumber desc)
    (typeName (descParent self) (fieldType desc))
    (case fieldDefaultValue desc of { Nothing -> ""; Just v -> " = " ++ valueString v; })
    (annotationsCode self)
    (case fieldOffset desc of
        PointerOffset o -> printf "ptr[%d]" o
        InlineCompositeOffset dataOffset pointerOffset dataSize pointerSize ->
            let dataBitOffset = dataOffset * dataSizeInBits (dataSectionAlignment dataSize)
            in printf "bits[%d, %d), ptrs[%d, %d)"
                dataBitOffset (dataBitOffset + dataSectionBits dataSize)
                pointerOffset (pointerOffset + pointerSize)
        VoidOffset -> "(none)"
        DataOffset dataSize offset -> let
            bits = dataSizeInBits dataSize
            in printf "bits[%d, %d)" (offset * bits) ((offset + 1) * bits))
    (case fieldUnion desc of { Nothing -> ""; Just (_, i) -> printf ", union tag = %d" i})

descToCode indent self@(DescUnion desc) = printf "%sunion %s@%d%s {  # [%d, %d)\n%s%s}\n" indent
    (unionName desc) (unionNumber desc)
    (annotationsCode self)
    (unionTagOffset desc * 16) (unionTagOffset desc * 16 + 16)
    (blockCode indent $ unionMembers desc)
    indent
descToCode indent self@(DescInterface desc) = printf "%sinterface %s @0x%016x%s {\n%s%s}\n" indent
    (interfaceName desc)
    (interfaceId desc)
    (annotationsCode self)
    (blockCode indent (interfaceMembers desc))
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
descToCode indent self@(DescAnnotation desc) = printf "%sannotation %s @0x%016x(%s): %s%s;\n" indent
    (annotationName desc)
    (annotationId desc)
    (delimit ", " $ map show $ Set.toList $ annotationTargets desc)
    (typeName (descParent self) (annotationType desc))
    (annotationsCode self)
descToCode _ (DescBuiltinType _) = error "Can't print code for builtin type."
descToCode _ DescBuiltinList = error "Can't print code for builtin type."
descToCode _ DescBuiltinInline = error "Can't print code for builtin type."
descToCode _ DescBuiltinInlineList = error "Can't print code for builtin type."
descToCode _ DescBuiltinInlineData = error "Can't print code for builtin type."

maybeBlockCode :: String -> [Desc] -> String
maybeBlockCode _ [] = ";\n"
maybeBlockCode indent statements = printf " {\n%s%s}\n" (blockCode indent statements) indent

blockCode :: String -> [Desc] -> String
blockCode indent = concatMap (descToCode ("  " ++ indent))

annotationCode :: Desc -> (Word64, (AnnotationDesc, ValueDesc)) -> String
annotationCode scope (_, (desc, VoidDesc)) =
    printf "$%s" (descQualifiedName scope (DescAnnotation desc))
annotationCode scope (_, (desc, val)) =
    printf "$%s(%s)" (descQualifiedName scope (DescAnnotation desc)) (valueString val)

annotationsCode desc = concatMap ((' ':) . annotationCode (descParent desc)) $ Map.toList
                     $ descAnnotations desc

instance Show FileDesc where { show desc = descToCode "" (DescFile desc) }
instance Show UsingDesc where { show desc = descToCode "" (DescUsing desc) }
instance Show ConstantDesc where { show desc = descToCode "" (DescConstant desc) }
instance Show EnumDesc where { show desc = descToCode "" (DescEnum desc) }
instance Show EnumerantDesc where { show desc = descToCode "" (DescEnumerant desc) }
instance Show StructDesc where { show desc = descToCode "" (DescStruct desc) }
instance Show FieldDesc where { show desc = descToCode "" (DescField desc) }
instance Show InterfaceDesc where { show desc = descToCode "" (DescInterface desc) }
instance Show MethodDesc where { show desc = descToCode "" (DescMethod desc) }
instance Show ParamDesc where { show desc = descToCode "" (DescParam desc) }
instance Show AnnotationDesc where { show desc = descToCode "" (DescAnnotation desc) }
