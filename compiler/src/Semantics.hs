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
import Data.Int (Int8, Int16, Int32, Int64)
import Data.Word (Word8, Word16, Word32, Word64)
import Data.Char (chr)
import Text.Printf(printf)
import Control.Monad(join)

type ByteString = [Word8]

data Desc = DescFile FileDesc
          | DescAlias AliasDesc
          | DescConstant ConstantDesc
          | DescEnum EnumDesc
          | DescEnumValue EnumValueDesc
          | DescClass ClassDesc
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
descName (DescClass     d) = className d
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
descParent (DescEnumValue d) = enumValueParent d
descParent (DescClass     d) = classParent d
descParent (DescField     d) = fieldParent d
descParent (DescInterface d) = interfaceParent d
descParent (DescMethod    d) = methodParent d
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
                 | BuiltinText | BuiltinBytes
                 deriving (Show, Enum, Bounded, Eq)

builtinTypes = [minBound::BuiltinType .. maxBound::BuiltinType]

-- Get in-language name of type.
builtinTypeName :: BuiltinType -> String
builtinTypeName = List.drop 7 . show  -- drop "Builtin" prefix

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
               | BytesDesc ByteString
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
valueString (BytesDesc   s) = show (map (chr . fromIntegral) s)

data TypeDesc = BuiltinType BuiltinType
              | EnumType EnumDesc
              | ClassType ClassDesc
              | InterfaceType InterfaceDesc
              | ListType TypeDesc

-- Render the type descriptor's name as a string, appropriate for use in the given scope.
typeName :: Desc -> TypeDesc -> String
typeName _ (BuiltinType t) = builtinTypeName t  -- TODO:  Check for shadowing.
typeName scope (EnumType desc) = descQualifiedName scope (DescEnum desc)
typeName scope (ClassType desc) = descQualifiedName scope (DescClass desc)
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
    , fileClasses :: [ClassDesc]
    , fileInterfaces :: [InterfaceDesc]
    , fileOptions :: OptionMap
    , fileMembers :: [Desc]
    , fileMemberMap :: MemberMap
    , fileImportMap :: Map.Map String FileDesc
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
    , enumMembers :: [Desc]
    , enumMemberMap :: MemberMap
    }

data EnumValueDesc = EnumValueDesc
    { enumValueName :: String
    , enumValueParent :: Desc
    , enumValueNumber :: Integer
    , enumValueOptions :: OptionMap
    }

data ClassDesc = ClassDesc
    { className :: String
    , classParent :: Desc
    , classFields :: [FieldDesc]
    , classNestedAliases :: [AliasDesc]
    , classNestedConstants :: [ConstantDesc]
    , classNestedEnums :: [EnumDesc]
    , classNestedClasses :: [ClassDesc]
    , classNestedInterfaces :: [InterfaceDesc]
    , classOptions :: OptionMap
    , classMembers :: [Desc]
    , classMemberMap :: MemberMap
    }

data FieldDesc = FieldDesc
    { fieldName :: String
    , fieldParent :: Desc
    , fieldNumber :: Integer
    , fieldType :: TypeDesc
    , fieldDefaultValue :: Maybe ValueDesc
    , fieldOptions :: OptionMap
    }

data InterfaceDesc = InterfaceDesc
    { interfaceName :: String
    , interfaceParent :: Desc
    , interfaceMethods :: [MethodDesc]
    , interfaceNestedAliases :: [AliasDesc]
    , interfaceNestedConstants :: [ConstantDesc]
    , interfaceNestedEnums :: [EnumDesc]
    , interfaceNestedClasses :: [ClassDesc]
    , interfaceNestedInterfaces :: [InterfaceDesc]
    , interfaceOptions :: OptionMap
    , interfaceMembers :: [Desc]
    , interfaceMemberMap :: MemberMap
    }

data MethodDesc = MethodDesc
    { methodName :: String
    , methodParent :: Desc
    , methodNumber :: Integer
    , methodParams :: [(String, TypeDesc, Maybe ValueDesc)]
    , methodReturnType :: TypeDesc
    , methodOptions :: OptionMap
    }

type OptionMap = Map.Map String OptionAssignmentDesc

data OptionAssignmentDesc = OptionAssignmentDesc
    { optionAssignmentOption :: OptionDesc
    , optionAssignmentValue :: ValueDesc
    }

data OptionDesc = OptionDesc
    { optionName :: String
    , optionParent :: Desc
    , optionId :: String
    , optionType :: TypeDesc
    , optionDefaultValue :: Maybe ValueDesc
    }

-- TODO:  Print options as well as members.  Will be ugly-ish.
descToCode :: String -> Desc -> String
descToCode indent (DescFile desc) = concatMap (descToCode indent) (fileMembers desc)
descToCode indent (DescAlias desc) = printf "%susing %s = %s;\n" indent
    (aliasName desc)
    (descQualifiedName (aliasParent desc) (aliasTarget desc))
descToCode indent (DescConstant desc) = printf "%sconst %s: %s = %s;\n" indent
    (constantName desc)
    (typeName (constantParent desc) (constantType desc))
    (valueString (constantValue desc))
descToCode indent (DescEnum desc) = printf "%senum %s {\n%s%s}\n" indent
    (enumName desc)
    (concatMap (descToCode ("  " ++ indent)) (enumMembers desc))
    indent
descToCode indent (DescEnumValue desc) = printf "%s%s = %d;\n" indent
    (enumValueName desc) (enumValueNumber desc)
descToCode indent (DescClass desc) = printf "%sclass %s {\n%s%s}\n" indent
    (className desc)
    (concatMap (descToCode ("  " ++ indent)) (classMembers desc))
    indent
descToCode indent (DescField desc) = printf "%s%s@%d: %s%s;\n" indent
    (fieldName desc) (fieldNumber desc)
    (typeName (fieldParent desc) (fieldType desc))
    (case fieldDefaultValue desc of { Nothing -> ""; Just v -> " = " ++ valueString v; })
descToCode indent (DescInterface desc) = printf "%sinterface %s {\n%s%s}\n" indent
    (interfaceName desc)
    (concatMap (descToCode ("  " ++ indent)) (interfaceMembers desc))
    indent
descToCode indent (DescMethod desc) = printf "%s%s@%d(%s): %s;\n" indent
    (methodName desc) (methodNumber desc)
    (delimit (map paramToCode (methodParams desc)))
    (typeName (methodParent desc) (methodReturnType desc)) where
        delimit [] = ""
        delimit (h:t) = h ++ concatMap (", " ++) t
        paramToCode (name, t, Nothing) = printf "%s: %s" name (typeName (methodParent desc) t)
        paramToCode (name, t, Just v) = printf "%s: %s = %s"
            name (typeName (methodParent desc) t) (valueString v)
descToCode _ (DescOption _) = error "options not implemented"
descToCode _ (DescBuiltinType _) = error "Can't print code for builtin type."
descToCode _ DescBuiltinList = error "Can't print code for builtin type."

instance Show FileDesc where { show desc = descToCode "" (DescFile desc) }
instance Show AliasDesc where { show desc = descToCode "" (DescAlias desc) }
instance Show ConstantDesc where { show desc = descToCode "" (DescConstant desc) }
instance Show EnumDesc where { show desc = descToCode "" (DescEnum desc) }
instance Show EnumValueDesc where { show desc = descToCode "" (DescEnumValue desc) }
instance Show ClassDesc where { show desc = descToCode "" (DescClass desc) }
instance Show FieldDesc where { show desc = descToCode "" (DescField desc) }
instance Show InterfaceDesc where { show desc = descToCode "" (DescInterface desc) }
instance Show MethodDesc where { show desc = descToCode "" (DescMethod desc) }
