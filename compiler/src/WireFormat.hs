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

module WireFormat where

import Data.List(sortBy, minimum)
import Data.Maybe(maybe)
import qualified Data.Map as Map
import qualified Data.Set as Set
import Semantics

-- Is this field a non-retroactive member of a union?  If so, its default value is not written.
isNonRetroUnionMember (FieldDesc {fieldNumber = n, fieldUnion = Just u}) = n > unionNumber u
isNonRetroUnionMember _ = False

-- What is this union's default tag value?  If there is a retroactive field, it is that field's
-- number, otherwise it is the union's number (meaning no field set).
unionDefault desc = max (minimum $ map fieldNumber $ unionFields desc) (unionNumber desc)

encodeStruct desc assignments = result where
    explicitlyAssignedNums = Set.fromList [fieldNumber desc | (desc, _) <- assignments]
    explicitlyAssignedUnions = Set.fromList
        [unionNumber u | (FieldDesc {fieldUnion = Just u}, _) <- assignments]

    -- Was this field explicitly assigned, or was another member of the same union explicitly
    -- assigned?  If so, its default value is not written.
    isExplicitlyAssigned (FieldDesc {fieldNumber = n, fieldUnion = u}) =
        Set.member n explicitlyAssignedNums ||
        maybe False (flip Set.member explicitlyAssignedUnions . unionNumber) u

    -- Values explicitly assigned.
    explicitValues = [(fieldOffset f, v) | (f, v) <- assignments]

    -- Values from defaults.
    defaultValues = [(o, v)
        | field@(FieldDesc { fieldOffset = o, fieldDefaultValue = Just v}) <- structFields desc
        , not $ isExplicitlyAssigned field
        , not $ isNonRetroUnionMember field ]

    -- Values of union tags.
    unionValues = [(unionTagOffset u, UInt8Desc n)
                  | (FieldDesc {fieldUnion = Just u, fieldNumber = n}, _) <- assignments]

    -- Default values of union dacs.
    unionDefaultValues = [(unionTagOffset u, unionDefault u) | u <- structUnions desc
                         , not $ Set.member (unionNumber u) explicitlyAssignedUnions]

    allValues = explicitValues ++ defaultValues ++ unionValues ++ unionDefaultValues

    allData = [ (o * sizeInBits (fieldValueSize v)) v
              | (o, v) <- allValues, fieldValueSize v /= SizeReference ]
    allReferences = [ (o, v) | (o, v) <- allValues, fieldValueSize v == SizeReference ]

    compareValues (o1, _) (o2, _) = compare o1 o2
    sortedData = sortBy compareValues allData
    sortedReferences = sortBy compareValues allReferences

    result = encodeData sortedData ++ encodeReferences sortedReferences
