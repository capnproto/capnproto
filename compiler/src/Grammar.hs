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

module Grammar where

import Token (Located)
import Data.Maybe (maybeToList)
import Data.Word (Word64)

data DeclName = AbsoluteName (Located String)
              | RelativeName (Located String)
              | ImportName (Located String)
              | MemberName DeclName (Located String)
              deriving (Show)

declNameImport :: DeclName -> Maybe (Located String)
declNameImport (AbsoluteName _) = Nothing
declNameImport (RelativeName _) = Nothing
declNameImport (ImportName s) = Just s
declNameImport (MemberName parent _) = declNameImport parent

data TypeParameter = TypeParameterType TypeExpression
                   | TypeParameterInteger Integer
                   deriving (Show)
data TypeExpression = TypeExpression DeclName [TypeParameter]
                    deriving (Show)

typeParameterImports :: TypeParameter -> [Located String]
typeParameterImports (TypeParameterType t) = typeImports t
typeParameterImports (TypeParameterInteger _) = []

typeImports :: TypeExpression -> [Located String]
typeImports (TypeExpression name params) =
    maybeToList (declNameImport name) ++ concatMap typeParameterImports params

data Annotation = Annotation DeclName (Located FieldValue) deriving(Show)

annotationImports (Annotation name _) = maybeToList $ declNameImport name

data FieldValue = VoidFieldValue
                | BoolFieldValue Bool
                | IntegerFieldValue Integer
                | FloatFieldValue Double
                | StringFieldValue String
                | IdentifierFieldValue String
                | ListFieldValue [Located FieldValue]
                | RecordFieldValue [(Located String, Located FieldValue)]
                | UnionFieldValue String FieldValue
                deriving (Show)

data ParamDecl = ParamDecl String TypeExpression [Annotation] (Maybe (Located FieldValue))
               deriving (Show)

paramImports (ParamDecl _ t ann _) = typeImports t ++ concatMap annotationImports ann

data AnnotationTarget = FileAnnotation
                      | ConstantAnnotation
                      | EnumAnnotation
                      | EnumerantAnnotation
                      | StructAnnotation
                      | FieldAnnotation
                      | UnionAnnotation
                      | InterfaceAnnotation
                      | MethodAnnotation
                      | ParamAnnotation
                      | AnnotationAnnotation
                      deriving(Eq, Ord, Bounded, Enum)

instance Show AnnotationTarget where
    show FileAnnotation = "file"
    show ConstantAnnotation = "const"
    show EnumAnnotation = "enum"
    show EnumerantAnnotation = "enumerant"
    show StructAnnotation = "struct"
    show FieldAnnotation = "field"
    show UnionAnnotation = "union"
    show InterfaceAnnotation = "interface"
    show MethodAnnotation = "method"
    show ParamAnnotation = "param"
    show AnnotationAnnotation = "annotation"

data Declaration = UsingDecl (Located String) DeclName
                 | ConstantDecl (Located String) TypeExpression [Annotation] (Located FieldValue)
                 | EnumDecl (Located String) (Maybe (Located Word64)) [Annotation] [Declaration]
                 | EnumerantDecl (Located String) (Located Integer) [Annotation]
                 | StructDecl (Located String) (Maybe (Located Word64))
                              (Maybe (Located (Integer, Integer))) [Annotation] [Declaration]
                 | FieldDecl (Located String) (Located Integer)
                             TypeExpression [Annotation] (Maybe (Located FieldValue))
                 | UnionDecl (Located String) (Located Integer) [Annotation] [Declaration]
                 | InterfaceDecl (Located String) (Maybe (Located Word64))
                                 [Annotation] [Declaration]
                 | MethodDecl (Located String) (Located Integer) [ParamDecl]
                              TypeExpression [Annotation]
                 | AnnotationDecl (Located String) (Maybe (Located Word64)) TypeExpression
                                  [Annotation] [AnnotationTarget]
                 deriving (Show)

declarationName :: Declaration -> Maybe (Located String)
declarationName (UsingDecl n _)            = Just n
declarationName (ConstantDecl n _ _ _)     = Just n
declarationName (EnumDecl n _ _ _)         = Just n
declarationName (EnumerantDecl n _ _)      = Just n
declarationName (StructDecl n _ _ _ _)     = Just n
declarationName (FieldDecl n _ _ _ _)      = Just n
declarationName (UnionDecl n _ _ _)        = Just n
declarationName (InterfaceDecl n _ _ _)    = Just n
declarationName (MethodDecl n _ _ _ _)     = Just n
declarationName (AnnotationDecl n _ _ _ _) = Just n

declImports :: Declaration -> [Located String]
declImports (UsingDecl _ name) = maybeToList (declNameImport name)
declImports (ConstantDecl _ t ann _) = typeImports t ++ concatMap annotationImports ann
declImports (EnumDecl _ _ ann decls) = concatMap annotationImports ann ++ concatMap declImports decls
declImports (EnumerantDecl _ _ ann) = concatMap annotationImports ann
declImports (StructDecl _ _ _ ann decls) = concatMap annotationImports ann ++
                                           concatMap declImports decls
declImports (FieldDecl _ _ t ann _) = typeImports t ++ concatMap annotationImports ann
declImports (UnionDecl _ _ ann decls) = concatMap annotationImports ann ++
                                        concatMap declImports decls
declImports (InterfaceDecl _ _ ann decls) = concatMap annotationImports ann ++
                                            concatMap declImports decls
declImports (MethodDecl _ _ params t ann) =
    concat [concatMap paramImports params, typeImports t, concatMap annotationImports ann]
declImports (AnnotationDecl _ _ t ann _) = typeImports t ++ concatMap annotationImports ann
