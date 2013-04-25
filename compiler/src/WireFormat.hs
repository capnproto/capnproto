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
import Data.Function(on)
import Semantics
import Data.Binary.IEEE754(floatToWord, doubleToWord)
import qualified Codec.Binary.UTF8.String as UTF8

byte :: (Integral a, Bits a) => a -> Int -> Word8
byte i amount = fromIntegral (shiftR i (amount * 8))

bytes :: (Integral a, Bits a) => a -> Int -> [Word8]
bytes i count = map (byte i) [0..(count - 1)]

padToWord b = let
    trailing = mod (length b) 8
    in if trailing == 0
        then b
        else b ++ replicate (8 - trailing) 0

data EncodedData = EncodedBit Bool
                 | EncodedBytes [Word8]

xorData (EncodedBit a) (EncodedBit b) = EncodedBit (a /= b)
xorData (EncodedBytes a) (EncodedBytes b) = EncodedBytes (zipWith xor a b)
xorData _ _ = error "Value type mismatch when xor'ing."

encodeDataValue :: TypeDesc -> ValueDesc -> EncodedData
encodeDataValue _ VoidDesc = EncodedBytes []
encodeDataValue _ (BoolDesc v) = EncodedBit v
encodeDataValue _ (Int8Desc v) = EncodedBytes $ bytes v 1
encodeDataValue _ (Int16Desc v) = EncodedBytes $ bytes v 2
encodeDataValue _ (Int32Desc v) = EncodedBytes $ bytes v 4
encodeDataValue _ (Int64Desc v) = EncodedBytes $ bytes v 8
encodeDataValue _ (UInt8Desc v) = EncodedBytes $ bytes v 1
encodeDataValue _ (UInt16Desc v) = EncodedBytes $ bytes v 2
encodeDataValue _ (UInt32Desc v) = EncodedBytes $ bytes v 4
encodeDataValue _ (UInt64Desc v) = EncodedBytes $ bytes v 8
encodeDataValue _ (Float32Desc v) = EncodedBytes $ bytes (floatToWord v) 4
encodeDataValue _ (Float64Desc v) = EncodedBytes $ bytes (doubleToWord v) 8
encodeDataValue _ (TextDesc _) = error "Not fixed-width data."
encodeDataValue _ (DataDesc _) = error "Not fixed-width data."
encodeDataValue _ (EnumerantValueDesc v) = EncodedBytes $ bytes (enumerantNumber v) 2
encodeDataValue (StructType desc) (StructValueDesc assignments) = let
    (dataBytes, refBytes, childBytes) = encodeStruct desc assignments 0
    in if null refBytes && null childBytes
        then EncodedBytes dataBytes
        else error "encodeDataValue called on struct that wasn't plain data."
encodeDataValue (InlineStructType desc) v = encodeDataValue (StructType desc) v
encodeDataValue _ (StructValueDesc _) = error "Type/value mismatch."
encodeDataValue _ (ListDesc _) = error "Not fixed-width data."

encodeMaskedDataValue t v Nothing = encodeDataValue t v
encodeMaskedDataValue t v (Just d) = xorData (encodeDataValue t v) (encodeDataValue t d)

encodePointerValue :: TypeDesc -> ValueDesc -> (Integer -> [Word8], [Word8])
encodePointerValue _ (TextDesc text) = let
    encoded = UTF8.encode text ++ [0]
    in (encodeListReference (SizeData Size8) (genericLength encoded), padToWord encoded)
encodePointerValue _ (DataDesc d) =
    (encodeListReference (SizeData Size8) (genericLength d), padToWord d)
encodePointerValue (StructType desc) (StructValueDesc assignments) = let
    (dataBytes, refBytes, childBytes) = encodeStruct desc assignments 0
    in (encodeStructReference desc, concat [padToWord dataBytes, refBytes, childBytes])
encodePointerValue (InlineStructType desc) v = encodePointerValue (StructType desc) v
encodePointerValue (ListType elementType) (ListDesc items) =
    (encodeListReference (elementSize elementType) (genericLength items),
     encodeList elementType items)
encodePointerValue _ _ = error "Unknown pointer type."

-- Given a sorted list of (bitOffset, data), pack into a byte array.
packBytes :: Integer                     -- Total size of array to pack, in bits.
          -> [(Integer, EncodedData)]    -- (offset, data) pairs to pack.  Must be in order.
          -> [Word8]
packBytes size = loop 0 where
    loop :: Integer -> [(Integer, EncodedData)] -> [Word8]
    loop bit [] | bit <= size = genericReplicate (div (size - bit + 7) 8) 0
    loop bit [] | bit > size = error "Data values overran size."
    loop bit values@((offset, _):_) | offset >= bit + 8 = 0:loop (bit + 8) values
    loop bit ((offset, EncodedBit True):rest) = let
        firstByte:restBytes = loop bit rest
        in setBit firstByte (fromIntegral (offset - bit)) : restBytes
    loop bit ((_, EncodedBit False):rest) = loop bit rest
    loop bit ((offset, EncodedBytes encoded):rest) | offset == bit =
        encoded ++ loop (bit + genericLength encoded * 8) rest
    loop _ _ = error "Data values overlapped."

bytesToWords i = if mod i 8 == 0 then div i 8
    else error "Byte count did not divide evenly into words."

packPointers :: Integer   -- Total number of pointers to pack.
             -> [(Integer, (Integer -> [Word8], [Word8]))]
             -> Integer   -- Word offset from end of pointer array to child area.
             -> ([Word8], [Word8])
packPointers size items o = loop 0 items (o + size - 1) where
    loop :: Integer -> [(Integer, (Integer -> [Word8], [Word8]))] -> Integer -> ([Word8], [Word8])
    loop idx ((pos, (mkptrs, child)):rest) childOff | idx == pos = let
        ptrs = mkptrs childOff
        ptrCount = bytesToWords (genericLength ptrs)
        newChildOff = childOff - ptrCount + bytesToWords (genericLength child)
        (restPtrs, restChildren) = loop (idx + ptrCount) rest newChildOff
        in (ptrs ++ restPtrs, child ++ restChildren)
    loop idx rest@((pos, _):_) childOff = let
        padCount = pos - idx
        (restPtrs, restChildren) = loop pos rest (childOff - padCount)
        in (genericReplicate (padCount * 8) 0 ++ restPtrs, restChildren)
    loop idx [] _ = (genericReplicate ((size - idx) * 8) 0, [])

encodeStructReference desc offset =
    bytes (offset * 4 + structTag) 4 ++
    bytes (dataSectionWordSize $ structDataSize desc) 2 ++
    bytes (structPointerCount desc) 2

encodeListReference elemSize@(SizeInlineComposite ds rc) elementCount offset =
    bytes (offset * 4 + listTag) 4 ++
    bytes (fieldSizeEnum elemSize + shiftL (elementCount * (dataSectionWordSize ds + rc)) 3) 4
encodeListReference elemSize elementCount offset =
    bytes (offset * 4 + listTag) 4 ++
    bytes (fieldSizeEnum elemSize + shiftL elementCount 3) 4

fieldSizeEnum SizeVoid = 0
fieldSizeEnum (SizeData Size1) = 1
fieldSizeEnum (SizeData Size8) = 2
fieldSizeEnum (SizeData Size16) = 3
fieldSizeEnum (SizeData Size32) = 4
fieldSizeEnum (SizeData Size64) = 5
fieldSizeEnum SizeReference = 6
fieldSizeEnum (SizeInlineComposite _ _) = 7

structTag = 0
listTag = 1

-- childOffset = number of words between the last reference and the location where children will
-- be allocated.
encodeStruct desc assignments childOffset = let
    dataSize = dataSectionBits $ structDataSize desc
    dataSection = packBytes dataSize $ sortBy (compare `on` fst)
                $ structDataSectionValues assignments

    pointerCount = structPointerCount desc
    (pointerSection, children) = packPointers pointerCount
        (sortBy (compare `on` fst) $ structPointerSectionValues assignments)
        childOffset

    in (dataSection, pointerSection, children)

dataBitOffset (DataOffset size off) = dataSizeInBits size * off
dataBitOffset (InlineCompositeOffset off _ dataSectionSize _) =
    off * dataSizeInBits (dataSectionAlignment dataSectionSize)
dataBitOffset _ = error "Not a data field."

structDataSectionValues assignments = let
    simpleValues = [(dataBitOffset $ fieldOffset f,
                     encodeMaskedDataValue (fieldType f) v (fieldDefaultValue f))
                   | (f@FieldDesc { fieldOffset = DataOffset _ _ }, v) <- assignments]

    inlineStructValues = do  -- List monad!
        (FieldDesc { fieldOffset = InlineCompositeOffset off _ sectionSize _ },
            StructValueDesc v) <- assignments
        let bitOffset = off * dataSizeInBits (dataSectionAlignment sectionSize)
        (pos, v2) <- structDataSectionValues v
        return (pos + bitOffset, v2)

    unionTags = [(unionTagOffset u * 16,
                  encodeDataValue (BuiltinType BuiltinUInt16) (UInt16Desc $ fromIntegral n))
                | (FieldDesc {fieldUnion = Just (u, n)}, _) <- assignments]

    in simpleValues ++ inlineStructValues ++ unionTags

structPointerSectionValues :: [(FieldDesc, ValueDesc)] -> [(Integer, (Integer -> [Word8], [Word8]))]
structPointerSectionValues assignments = let
    simpleValues = [(off, encodePointerValue (fieldType f) v)
                   | (f@FieldDesc { fieldOffset = PointerOffset off }, v) <- assignments]

    inlineStructValues = do  -- List monad!
        (FieldDesc { fieldOffset = InlineCompositeOffset _ off _ _ },
            StructValueDesc v) <- assignments
        (pos, v2) <- structPointerSectionValues v
        return (pos + off, v2)

    in simpleValues ++ inlineStructValues

encodeList elementType elements = case elementSize elementType of
    SizeVoid -> []
    SizeInlineComposite _ _ -> let
        handleStructType desc = let
            count = genericLength elements
            tag = encodeStructReference desc count
            (elemBytes, childBytes) = encodeStructList 0 desc [v | StructValueDesc v <- elements]
            in concat [tag, elemBytes, childBytes]
        in case elementType of
            StructType desc -> handleStructType desc
            InlineStructType desc -> handleStructType desc
            _ -> error "Only structs can be inline composites."
    SizeReference -> refBytes ++ childBytes where
        encodedElements = zip [0..] $ map (encodePointerValue elementType) elements
        (refBytes, childBytes) = packPointers (genericLength elements) encodedElements 0
    SizeData size -> let
        bits = dataSizeInBits size
        encodedElements = zip [0,bits..] $ map (encodeDataValue elementType) elements
        in padToWord $ packBytes (genericLength elements * bits) encodedElements

-- Encode an inline-composite struct list.  Not used in cases where the struct is data-only and
-- fits into 32 bits or less.
encodeStructList :: Integer -> StructDesc -> [[(FieldDesc, ValueDesc)]] -> ([Word8], [Word8])
encodeStructList o desc elements = loop (o + eSize * genericLength elements) elements where
    eSize = dataSectionWordSize (structDataSize desc) + structPointerCount desc
    loop _ [] = ([], [])
    loop offset (element:rest) = let
        offsetFromElementEnd = offset - eSize
        (dataBytes, refBytes, childBytes) = encodeStruct desc element offsetFromElementEnd
        childLen = genericLength childBytes
        childWordLen = if mod childLen 8 == 0
            then div childLen 8
            else error "Child not word-aligned."
        (restBytes, restChildren) = loop (offsetFromElementEnd + childWordLen) rest
        in (padToWord dataBytes ++ refBytes ++ restBytes, childBytes ++ restChildren)

encodeMessage (StructType desc) (StructValueDesc assignments) = let
    (dataBytes, refBytes, childBytes) = encodeStruct desc assignments 0
    in concat [encodeStructReference desc (0::Integer), padToWord dataBytes, refBytes, childBytes]
encodeMessage (InlineStructType desc) val = encodeMessage (StructType desc) val
encodeMessage (ListType elementType) (ListDesc elements) =
    encodeListReference (elementSize elementType) (genericLength elements) (0::Integer) ++
    encodeList elementType elements
encodeMessage _ _ = error "Not a message."
