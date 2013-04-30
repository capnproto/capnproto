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
import Data.Bits(shiftL, Bits, setBit, xor)
import Data.Function(on)
import Semantics
import Data.Binary.IEEE754(floatToWord, doubleToWord)
import Text.Printf(printf)
import qualified Codec.Binary.UTF8.String as UTF8
import Util(intToBytes)

padToWord b = let
    trailing = mod (length b) 8
    in if trailing == 0
        then b
        else b ++ replicate (8 - trailing) 0

data EncodedData = EncodedBit Bool
                 | EncodedBytes [Word8]
                 deriving(Show)

xorData (EncodedBit a) (EncodedBit b) = EncodedBit (a /= b)
xorData (EncodedBytes a) (EncodedBytes b) = EncodedBytes (zipWith xor a b)
xorData _ _ = error "Value type mismatch when xor'ing."

encodeDataValue :: TypeDesc -> ValueDesc -> EncodedData
encodeDataValue _ VoidDesc = EncodedBytes []
encodeDataValue _ (BoolDesc v) = EncodedBit v
encodeDataValue _ (Int8Desc v) = EncodedBytes $ intToBytes v 1
encodeDataValue _ (Int16Desc v) = EncodedBytes $ intToBytes v 2
encodeDataValue _ (Int32Desc v) = EncodedBytes $ intToBytes v 4
encodeDataValue _ (Int64Desc v) = EncodedBytes $ intToBytes v 8
encodeDataValue _ (UInt8Desc v) = EncodedBytes $ intToBytes v 1
encodeDataValue _ (UInt16Desc v) = EncodedBytes $ intToBytes v 2
encodeDataValue _ (UInt32Desc v) = EncodedBytes $ intToBytes v 4
encodeDataValue _ (UInt64Desc v) = EncodedBytes $ intToBytes v 8
encodeDataValue _ (Float32Desc v) = EncodedBytes $ intToBytes (floatToWord v) 4
encodeDataValue _ (Float64Desc v) = EncodedBytes $ intToBytes (doubleToWord v) 8
encodeDataValue _ (TextDesc _) = error "Not fixed-width data."
encodeDataValue _ (DataDesc _) = error "Not fixed-width data."
encodeDataValue _ (EnumerantValueDesc v) = EncodedBytes $ intToBytes (enumerantNumber v) 2
encodeDataValue _ (StructValueDesc _) = error "Not fixed-width data."
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
    in (encodeStructReference desc, concat [dataBytes, refBytes, childBytes])
encodePointerValue (InlineStructType _) _ =
    error "Tried to encode inline struct as a pointer."
encodePointerValue (ListType elementType) (ListDesc items) = encodeList elementType items
encodePointerValue (InlineListType _ _) _ =
    error "Tried to encode inline list as a pointer."
encodePointerValue (InlineDataType _) _ =
    error "Tried to encode inline data as a pointer."
encodePointerValue _ _ = error "Unknown pointer type."

-- Given a sorted list of (bitOffset, data), pack into a byte array.
packBytes :: Integer                     -- Total size of array to pack, in bits.
          -> [(Integer, EncodedData)]    -- (offset, data) pairs to pack.  Must be in order.
          -> [Word8]
packBytes size items = padToWord $ loop 0 items where
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
    loop bit rest = error
        (printf "Data values overlapped @%d: %s\n\n%s" bit (show rest) (show items))

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
    intToBytes (offset * 4 + structTag) 4 ++
    intToBytes (dataSectionWordSize $ structDataSize desc) 2 ++
    intToBytes (structPointerCount desc) 2

encodeInlineStructListReference elementDataSize elementPointerCount elementCount offset = let
    dataBits = dataSectionBits elementDataSize * elementCount
    dataWords = div (dataBits + 63) 64
    in intToBytes (offset * 4 + structTag) 4 ++
       intToBytes dataWords 2 ++
       intToBytes (elementPointerCount * elementCount) 2

encodeListReference elemSize@(SizeInlineComposite ds rc) elementCount offset =
    intToBytes (offset * 4 + listTag) 4 ++
    intToBytes (fieldSizeEnum elemSize + shiftL (elementCount * (dataSectionWordSize ds + rc)) 3) 4
encodeListReference elemSize elementCount offset =
    intToBytes (offset * 4 + listTag) 4 ++
    intToBytes (fieldSizeEnum elemSize + shiftL elementCount 3) 4

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

    inlineCompositeValues = do  -- List monad!
        (FieldDesc { fieldType = t
                   , fieldOffset = InlineCompositeOffset off _ sectionSize _ }, v) <- assignments
        let bitOffset = off * dataSizeInBits (dataSectionAlignment sectionSize)
        (pos, v2) <- case (t, v) of
            (InlineStructType _, StructValueDesc v2) -> structDataSectionValues v2
            (InlineListType t2 _, ListDesc v2) -> inlineListDataSectionValues t2 v2
            (InlineDataType _, DataDesc v2) -> [(0, EncodedBytes v2)]
            _ -> error "Non-inline-composite had inline-composite offset."
        return (pos + bitOffset, v2)

    unionTags = [(unionTagOffset u * 16,
                  encodeDataValue (BuiltinType BuiltinUInt16) (UInt16Desc $ fromIntegral n))
                | (FieldDesc {fieldUnion = Just (u, n)}, _) <- assignments]

    in simpleValues ++ inlineCompositeValues ++ unionTags

structPointerSectionValues :: [(FieldDesc, ValueDesc)] -> [(Integer, (Integer -> [Word8], [Word8]))]
structPointerSectionValues assignments = let
    simpleValues = [(off, encodePointerValue (fieldType f) v)
                   | (f@FieldDesc { fieldOffset = PointerOffset off }, v) <- assignments]

    inlineCompositeValues = do  -- List monad!
        (FieldDesc { fieldType = t
                   , fieldOffset = InlineCompositeOffset _ off _ _ }, v) <- assignments
        (pos, v2) <- case (t, v) of
            (InlineStructType _, StructValueDesc v2) -> structPointerSectionValues v2
            (InlineListType t2 _, ListDesc v2) -> inlineListPointerSectionValues t2 v2
            (InlineDataType _, DataDesc _) -> []
            _ -> error "Non-inline-composite had inline-composite offset."
        return (pos + off, v2)

    in simpleValues ++ inlineCompositeValues

------------------------------------------------------------------------------------------

encodeList :: TypeDesc                  -- Type of each element.
           -> [ValueDesc]               -- Element values.
           -> (Integer -> [Word8],      -- Encodes the pointer, given the offset.
               [Word8])                 -- Body bytes.

-- Encode a list of empty structs as void.
encodeList (StructType StructDesc {
        structDataSize = DataSectionWords 0, structPointerCount = 0 }) elements =
    (encodeListReference SizeVoid (genericLength elements), [])

-- Encode a list of sub-word data-only structs as a list of primitives.
encodeList (StructType desc@StructDesc { structDataSize = ds, structPointerCount = 0 }) elements
        | dataSectionBits ds <= 64 = let
    in (encodeListReference (SizeData $ dataSectionAlignment ds) (genericLength elements),
        inlineStructListDataSection desc elements)

-- Encode a list of single-pointer structs as a list of pointers.
encodeList (StructType desc@StructDesc {
        structDataSize = DataSectionWords 0, structPointerCount = 1 }) elements = let
    (refBytes, childBytes) = inlineStructListPointerSection desc elements
    in (encodeListReference SizeReference (genericLength elements), refBytes ++ childBytes)

-- Encode a list of any other sort of struct.
encodeList (StructType desc) elements = let
    count = genericLength elements
    tag = encodeStructReference desc count
    eSize = dataSectionWordSize (structDataSize desc) + structPointerCount desc
    structElems = [v | StructValueDesc v <- elements]
    (elemBytes, childBytes) = loop (eSize * genericLength structElems) structElems
    loop _ [] = ([], [])
    loop offset (element:rest) = let
        offsetFromElementEnd = offset - eSize
        (dataBytes, refBytes, childBytes2) = encodeStruct desc element offsetFromElementEnd
        childLen = genericLength childBytes2
        childWordLen = if mod childLen 8 == 0
            then div childLen 8
            else error "Child not word-aligned."
        (restBytes, restChildren) = loop (offsetFromElementEnd + childWordLen) rest
        in (dataBytes ++ refBytes ++ restBytes, childBytes2 ++ restChildren)
    in (encodeListReference (SizeInlineComposite (structDataSize desc) (structPointerCount desc))
                            (genericLength elements),
        concat [tag, elemBytes, childBytes])

encodeList (InlineStructType _) _ = error "Not supported:  List of inline structs."

-- Encode a list of inline lists by just concatenating all the elements.  The number of inner
-- lists can be determined at runtime by dividing the total size by the fixed inline list size.
-- Note that this means if you have something like List(InlineList(UInt8, 3)) and the list has
-- two elements, the total size will be 6 bytes -- we don't round the individual sub-lists up
-- to power-of-two boundaries.
encodeList (InlineListType (InlineStructType t) _) elements =
    encodeList (StructType t) (concat [l | ListDesc l <- elements])
encodeList (InlineListType t _) elements = encodeList t (concat [l | ListDesc l <- elements])

-- Encode a list of inline data.  Similar deal to above.
encodeList (InlineDataType _) elements =
    encodePointerValue (BuiltinType BuiltinData) (DataDesc $ concat [l | DataDesc l <- elements])

-- Encode primitive types.
encodeList elementType elements = let
    eSize = fieldSize elementType
    dataBytes = case eSize of
        SizeVoid -> []
        SizeInlineComposite _ _ -> error "All inline composites should have been handled above."
        SizeReference -> refBytes ++ childBytes where
            encodedElements = zip [0..] $ map (encodePointerValue elementType) elements
            (refBytes, childBytes) = packPointers (genericLength elements) encodedElements 0
        SizeData size -> let
            bits = dataSizeInBits size
            encodedElements = zip [0,bits..] $ map (encodeDataValue elementType) elements
            in packBytes (genericLength elements * bits) encodedElements
    in (encodeListReference eSize (genericLength elements), dataBytes)

---------------------------------------------

inlineListDataSectionValues elementType elements = case fieldSize elementType of
    SizeVoid -> []
    (SizeInlineComposite _ _) -> case elementType of
        InlineStructType desc -> inlineStructListDataSectionValues desc elements
        InlineListType t _ -> inlineListDataSectionValues t (concat [l | ListDesc l <- elements])
        InlineDataType _ -> [(0, EncodedBytes $ concat [l | DataDesc l <- elements])]
        _ -> error "Unknown inline composite type."
    SizeReference -> []
    SizeData size -> let
        bits = dataSizeInBits size
        in zip [0,bits..] $ map (encodeDataValue elementType) elements

inlineListPointerSectionValues elementType elements = case fieldSize elementType of
    SizeVoid -> []
    (SizeInlineComposite _ _) -> case elementType of
        InlineStructType desc -> inlineStructListPointerSectionValues desc elements
        InlineListType t _ -> inlineListPointerSectionValues t (concat [l | ListDesc l <- elements])
        InlineDataType _ -> []
        _ -> error "Unknown inline composite type."
    SizeReference -> zip [0..] $ map (encodePointerValue elementType) elements
    SizeData _ -> []

inlineStructListDataSection elementDesc elements =
    packBytes (genericLength elements * dataSectionBits (structDataSize elementDesc))
              (sortBy (compare `on` fst) $ inlineStructListDataSectionValues elementDesc elements)

inlineStructListDataSectionValues elementDesc elements = do
    let bits = dataSectionBits $ structDataSize elementDesc
    (i, StructValueDesc e) <- zip [0..] elements
    (off, v) <- structDataSectionValues e
    return (off + bits * i, v)

inlineStructListPointerSection elementDesc elements =
    packPointers
        (genericLength elements * structPointerCount elementDesc)
        (sortBy (compare `on` fst) $ inlineStructListPointerSectionValues elementDesc elements)
        0

inlineStructListPointerSectionValues elementDesc elements = do
    let ptrs = structPointerCount elementDesc
    (i, StructValueDesc e) <- zip [0..] elements
    (off, v) <- structPointerSectionValues e
    return (off + ptrs * i, v)

------------------------------------------------------------------------------------------

encodeMessage (StructType desc) (StructValueDesc assignments) = let
    (dataBytes, refBytes, childBytes) = encodeStruct desc assignments 0
    in concat [encodeStructReference desc (0::Integer), dataBytes, refBytes, childBytes]
encodeMessage (ListType elementType) (ListDesc elements) = let
    (ptr, listBytes) = encodeList elementType elements
    in ptr (0::Integer) ++ listBytes
encodeMessage _ _ = error "Not a message."
