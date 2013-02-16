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

data DeclName = AbsoluteName (Located String)
              | RelativeName (Located String)
              | ImportName (Located String)
              | MemberName DeclName (Located String)
              deriving (Show)

data TypeExpression = TypeExpression DeclName [TypeExpression]
                    deriving (Show)

data FieldValue = VoidFieldValue
                | BoolFieldValue Bool
                | IntegerFieldValue Integer
                | FloatFieldValue Double
                | StringFieldValue String
                | ArrayFieldValue [FieldValue]
                | RecordFieldValue [(String, FieldValue)]
                deriving (Show)

data Declaration = AliasDecl (Located String) DeclName
                 | ConstantDecl (Located String) TypeExpression (Located FieldValue)
                 | EnumDecl (Located String) [Declaration]
                 | EnumValueDecl (Located String) (Located Integer) [Declaration]
                 | ClassDecl (Located String) [Declaration]
                 | FieldDecl (Located String) (Located Integer)
                             TypeExpression (Maybe (Located FieldValue)) [Declaration]
                 | InterfaceDecl (Located String) [Declaration]
                 | MethodDecl (Located String) (Located Integer)
                              [(String, TypeExpression, Maybe (Located FieldValue))]
                              TypeExpression [Declaration]
                 | OptionDecl DeclName (Located FieldValue)
                 deriving (Show)
