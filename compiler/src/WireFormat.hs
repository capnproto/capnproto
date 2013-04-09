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

module WireFormat(encodeMessage) where

import Data.List(sortBy, genericLength, genericReplicate)
import Data.Word
import Data.Bits(shiftL, shiftR, Bits, setBit, xor)
import Semantics
import Data.Binary.IEEE754(floatToWord, doubleToWord)
import qualified Codec.Binary.UTF8.String as UTF8

--
byte :: (Integral a, Bits a) => a -> Int -> Word8
byte i amount = fromIntegral (shiftR i (amount * 8))

bytes :: (Integral a, Bits a) => a -> Int -> [Word8]
bytes i count = map (byte i) [0..(count - 1)]

padToWord b = let
    trailing = mod (length b) 8
    in if trailing == 0
        then b
        else b ++ replicate (8 - trailing) 0

roundUpToMultiple factor n = let
    remainder = mod n factor
    in if remainder == 0
        then n
        else n + (factor - remainder)

encodeDataValue :: ValueDesc -> [Word8]
encodeDataValue VoidDesc = []
encodeDataValue (BoolDesc _) = error "Bools must be handled specially."
encodeDataValue (Int8Desc v) = bytes v 1
encodeDataValue (Int16Desc v) = bytes v 2
encodeDataValue (Int32Desc v) = bytes v 4
encodeDataValue (Int64Desc v) = bytes v 8
encodeDataValue (UInt8Desc v) = bytes v 1
encodeDataValue (UInt16Desc v) = bytes v 2
encodeDataValue (UInt32Desc v) = bytes v 4
encodeDataValue (UInt64Desc v) = bytes v 8
encodeDataValue (Float32Desc v) = bytes (floatToWord v) 4
encodeDataValue (Float64Desc v) = bytes (doubleToWord v) 8
encodeDataValue (TextDesc _) = error "Not fixed-width data."
encodeDataValue (DataDesc _) = error "Not fixed-width data."
encodeDataValue (EnumValueValueDesc v) = bytes (enumValueNumber v) 2
encodeDataValue (StructValueDesc _) = error "Not fixed-width data."
encodeDataValue (ListDesc _) = error "Not fixed-width data."

encodeMaskedDataValue v Nothing = encodeDataValue v
encodeMaskedDataValue v (Just d) = zipWith xor (encodeDataValue v) (encodeDataValue d)

packBits :: Bits a => Int -> [(Bool, Maybe Bool)] -> a
packBits _ [] = 0
packBits offset ((True, Nothing):bits) = setBit (packBits (offset + 1) bits) offset
packBits offset ((False, Nothing):bits) = packBits (offset + 1) bits
packBits offset ((b, Just d):bits) = packBits offset ((b /= d, Nothing):bits)

-- The tuples are (offsetInBits, type, value, defaultValue) for each field to encode.
encodeData :: Integer -> [(Integer, TypeDesc, ValueDesc, Maybe ValueDesc)] -> [Word8]
encodeData size = loop 0 where
    loop bit [] | bit == size = []
    loop bit [] | bit > size = error "Data values overran size."
    loop bit [] = 0:loop (bit + 8) []
    loop bit rest@((valuePos, _, BoolDesc _, _):_) | valuePos == bit = let
        (bits, rest2) = popBits (bit + 8) rest
        in packBits 0 bits : loop (bit + 8) rest2
    loop bit ((valuePos, _, value, defaultValue):rest) | valuePos == bit =
        encodeMaskedDataValue value defaultValue ++
        loop (bit + sizeInBits (fieldValueSize value)) rest
    loop bit rest@((valuePos, _, _, _):_) | valuePos > bit = 0 : loop (bit + 8) rest
    loop _ _ = error "Data values were out-of-order."

    popBits limit ((valuePos, _, BoolDesc b, d):rest) | valuePos < limit = let
        (restBits, rest2) = popBits limit rest
        defaultB = fmap (\(BoolDesc b2) -> b2) d
        in ((b, defaultB):restBits, rest2)
    popBits _ rest = ([], rest)

encodeReferences :: Integer -> Integer -> [(Integer, TypeDesc, ValueDesc)] -> ([Word8], [Word8])
encodeReferences o size = loop 0 (o + size - 1) where
    loop idx offset ((pos, t, v):rest) | idx == pos = let
        (ref, obj) = case (t, v) of
            (StructType desc, StructValueDesc assignments) -> let
                (dataBytes, refBytes, childBytes) = encodeStruct desc assignments 0
                in (encodeStructReference desc offset, concat [dataBytes, refBytes, childBytes])
            (ListType elementType, ListDesc items) ->
                (encodeListReference (elementSize elementType) (genericLength items) offset,
                 encodeList elementType items)
            (BuiltinType BuiltinText, TextDesc text) -> let
                encoded = (UTF8.encode text ++ [0])
                in (encodeListReference Size8 (genericLength encoded) offset, padToWord encoded)
            (BuiltinType BuiltinData, DataDesc d) -> let
                in (encodeListReference Size8 (genericLength d) offset, padToWord d)
            _ -> error "Unknown reference type."
        len = genericLength obj
        wordLen = if mod len 8 == 0 then div len 8 else error "Child not word-aligned."
        (refs, objects) = loop (idx + 1) (offset + wordLen - 1) rest
        in (ref ++ refs, obj ++ objects)
    loop idx offset rest@((pos, _, _):_) = let
        padCount = pos - idx
        (refs, objects) = loop pos (offset - padCount) rest
        in (genericReplicate (padCount * 8) 0 ++ refs, objects)
    loop idx _ [] = (genericReplicate ((size - idx) * 8) 0, [])

encodeStructList :: Integer -> StructDesc -> [[(FieldDesc, ValueDesc)]] -> ([Word8], [Word8])
encodeStructList o desc elements = loop (o + eSize * genericLength elements) elements where
    eSize = packingSize $ structPacking desc
    loop _ [] = ([], [])
    loop offset (element:rest) = let
        offsetFromElementEnd = offset - eSize
        (dataBytes, refBytes, childBytes) = encodeStruct desc element offsetFromElementEnd
        childLen = genericLength childBytes
        childWordLen = if mod childLen 8 == 0
            then div childLen 8
            else error "Child not word-aligned."
        (restBytes, restChildren) = loop (offsetFromElementEnd + childWordLen) rest
        in (dataBytes ++ refBytes ++ restBytes, childBytes ++ restChildren)

encodeStructReference desc offset =
    bytes (offset * 4 + structTag) 4 ++
    bytes (packingDataSize $ structPacking desc) 2 ++
    bytes (packingReferenceCount $ structPacking desc) 2

encodeListReference elemSize@(SizeInlineComposite ds rc) elementCount offset =
    bytes (offset * 4 + listTag) 4 ++
    bytes (shiftL (fieldSizeEnum elemSize) 29 + elementCount * (ds + rc)) 4
encodeListReference elemSize elementCount offset =
    bytes (offset * 4 + listTag) 4 ++
    bytes (shiftL (fieldSizeEnum elemSize) 29 + elementCount) 4

fieldSizeEnum Size0 = 0
fieldSizeEnum Size1 = 1
fieldSizeEnum Size8 = 2
fieldSizeEnum Size16 = 3
fieldSizeEnum Size32 = 4
fieldSizeEnum Size64 = 5
fieldSizeEnum SizeReference = 6
fieldSizeEnum (SizeInlineComposite _ _) = 7

structTag = 0
listTag = 1

-- What is this union's default tag value?  If there is a retroactive field, it is that field's
-- number, otherwise it is the union's number (meaning no field set).
unionDefault desc = UInt8Desc $ fromIntegral $
    max (minimum $ map fieldNumber $ unionFields desc) (unionNumber desc)

-- childOffset = number of words between the last reference and the location where children will
-- be allocated.
encodeStruct desc assignments childOffset = (dataBytes, referenceBytes, children) where
    -- Values explicitly assigned.
    explicitValues = [(fieldOffset f, fieldType f, v, fieldDefaultValue f) | (f, v) <- assignments]

    -- Values of union tags.
    unionValues = [(unionTagOffset u, BuiltinType BuiltinUInt8, UInt8Desc $ fromIntegral n,
                      Just $ unionDefault u)
                  | (FieldDesc {fieldUnion = Just u, fieldNumber = n}, _) <- assignments]

    allValues = explicitValues ++ unionValues
    allData = [ (o * sizeInBits (fieldValueSize v), t, v, d)
              | (o, t, v, d) <- allValues, isDataFieldSize $ fieldValueSize v ]
    allReferences = [ (o, t, v) | (o, t, v, _) <- allValues
                    , not $ isDataFieldSize $ fieldValueSize v ]

    sortedData = sortBy compareDataValues allData
    compareDataValues (o1, _, _, _) (o2, _, _, _) = compare o1 o2
    sortedReferences = sortBy compareReferenceValues allReferences
    compareReferenceValues (o1, _, _) (o2, _, _) = compare o1 o2

    dataBytes = encodeData (packingDataSize (structPacking desc) * 64) sortedData
    (referenceBytes, children) = encodeReferences childOffset
        (packingReferenceCount $ structPacking desc) sortedReferences

encodeList elementType elements = case elementSize elementType of
    SizeInlineComposite _ _ -> case elementType of
        StructType desc -> let
            count = genericLength elements
            tag = encodeStructReference desc count
            (elemBytes, childBytes) = encodeStructList 0 desc [v | StructValueDesc v <- elements]
            in concat [tag, elemBytes, childBytes]
        _ -> error "Only structs can be inline composites."
    SizeReference -> refBytes ++ childBytes where
        (refBytes, childBytes) = encodeReferences 0 (genericLength elements)
                               $ zipWith (\i v -> (i, elementType, v)) [0..] elements
    size -> encodeData (roundUpToMultiple 64 (genericLength elements * sizeInBits size))
          $ zipWith (\i v -> (i * sizeInBits size, elementType, v, Nothing)) [0..] elements

encodeMessage (StructType desc) (StructValueDesc assignments) = let
    (dataBytes, refBytes, childBytes) = encodeStruct desc assignments 0
    in concat [encodeStructReference desc (0::Integer), dataBytes, refBytes, childBytes]
encodeMessage (ListType elementType) (ListDesc elements) =
    encodeListReference (elementSize elementType) (genericLength elements) (0::Integer) ++
    encodeList elementType elements
encodeMessage _ _ = error "Not a message."
