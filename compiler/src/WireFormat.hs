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

module WireFormat(encodeMessage, encodeSchema) where

import Data.List(sortBy, genericLength, genericReplicate)
import Data.Word
import Data.Bits(shiftL, Bits, setBit, xor)
import Data.Function(on)
import Data.Maybe(mapMaybe, listToMaybe, isNothing)
import Data.List(findIndices)
import qualified Data.Map as Map
import qualified Data.Set as Set
import Semantics
import Data.Binary.IEEE754(floatToWord, doubleToWord)
import Text.Printf(printf)
import qualified Codec.Binary.UTF8.String as UTF8
import Util(intToBytes)
import Grammar(AnnotationTarget(..))

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
    in (encodeListPointer (SizeData Size8) (genericLength encoded), padToWord encoded)
encodePointerValue _ (DataDesc d) =
    (encodeListPointer (SizeData Size8) (genericLength d), padToWord d)
encodePointerValue (StructType desc) (StructValueDesc assignments) = let
    (dataBytes, ptrBytes, childBytes) = encodeStruct desc assignments 0
    in (encodeStructPointer (structDataSize desc, structPointerCount desc),
        concat [dataBytes, ptrBytes, childBytes])
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

encodeStructPointer (dataSize, pointerCount) offset =
    intToBytes (offset * 4 + structTag) 4 ++
    intToBytes (dataSectionWordSize dataSize) 2 ++
    intToBytes pointerCount 2

encodeListPointer elemSize@(SizeInlineComposite ds rc) elementCount offset =
    intToBytes (offset * 4 + listTag) 4 ++
    intToBytes (fieldSizeEnum elemSize + shiftL (elementCount * (dataSectionWordSize ds + rc)) 3) 4
encodeListPointer elemSize elementCount offset =
    intToBytes (offset * 4 + listTag) 4 ++
    intToBytes (fieldSizeEnum elemSize + shiftL elementCount 3) 4

fieldSizeEnum SizeVoid = 0
fieldSizeEnum (SizeData Size1) = 1
fieldSizeEnum (SizeData Size8) = 2
fieldSizeEnum (SizeData Size16) = 3
fieldSizeEnum (SizeData Size32) = 4
fieldSizeEnum (SizeData Size64) = 5
fieldSizeEnum SizePointer = 6
fieldSizeEnum (SizeInlineComposite _ _) = 7

structTag = 0
listTag = 1

-- childOffset = number of words between the last pointer and the location where children will
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
    (encodeListPointer SizeVoid (genericLength elements), [])

-- Encode a list of sub-word data-only structs as a list of primitives.
encodeList (StructType desc@StructDesc { structDataSize = ds, structPointerCount = 0 }) elements
        | dataSectionBits ds <= 64 = let
    in (encodeListPointer (SizeData $ dataSectionAlignment ds) (genericLength elements),
        inlineStructListDataSection desc elements)

-- Encode a list of single-pointer structs as a list of pointers.
encodeList (StructType desc@StructDesc {
        structDataSize = DataSectionWords 0, structPointerCount = 1 }) elements = let
    (ptrBytes, childBytes) = inlineStructListPointerSection desc elements
    in (encodeListPointer SizePointer (genericLength elements), ptrBytes ++ childBytes)

-- Encode a list of any other sort of struct.
encodeList (StructType desc) elements = let
    count = genericLength elements
    tag = encodeStructPointer (structDataSize desc, structPointerCount desc) count
    eSize = dataSectionWordSize (structDataSize desc) + structPointerCount desc
    structElems = [v | StructValueDesc v <- elements]
    (elemBytes, childBytes) = loop (eSize * genericLength structElems) structElems
    loop _ [] = ([], [])
    loop offset (element:rest) = let
        offsetFromElementEnd = offset - eSize
        (dataBytes, ptrBytes, childBytes2) = encodeStruct desc element offsetFromElementEnd
        childLen = genericLength childBytes2
        childWordLen = if mod childLen 8 == 0
            then div childLen 8
            else error "Child not word-aligned."
        (restBytes, restChildren) = loop (offsetFromElementEnd + childWordLen) rest
        in (dataBytes ++ ptrBytes ++ restBytes, childBytes2 ++ restChildren)
    in (encodeListPointer (SizeInlineComposite (structDataSize desc) (structPointerCount desc))
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
        SizePointer -> ptrBytes ++ childBytes where
            encodedElements = zip [0..] $ map (encodePointerValue elementType) elements
            (ptrBytes, childBytes) = packPointers (genericLength elements) encodedElements 0
        SizeData size -> let
            bits = dataSizeInBits size
            encodedElements = zip [0,bits..] $ map (encodeDataValue elementType) elements
            in packBytes (genericLength elements * bits) encodedElements
    in (encodeListPointer eSize (genericLength elements), dataBytes)

---------------------------------------------

inlineListDataSectionValues elementType elements = case fieldSize elementType of
    SizeVoid -> []
    (SizeInlineComposite _ _) -> case elementType of
        InlineStructType desc -> inlineStructListDataSectionValues desc elements
        InlineListType t _ -> inlineListDataSectionValues t (concat [l | ListDesc l <- elements])
        InlineDataType _ -> [(0, EncodedBytes $ concat [l | DataDesc l <- elements])]
        _ -> error "Unknown inline composite type."
    SizePointer -> []
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
    SizePointer -> zip [0..] $ map (encodePointerValue elementType) elements
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
    (dataBytes, ptrBytes, childBytes) = encodeStruct desc assignments 0
    in concat [encodeStructPointer (structDataSize desc, structPointerCount desc) (0::Integer),
               dataBytes, ptrBytes, childBytes]
encodeMessage (ListType elementType) (ListDesc elements) = let
    (ptr, listBytes) = encodeList elementType elements
    in ptr (0::Integer) ++ listBytes
encodeMessage _ _ = error "Not a message."

------------------------------------------------------------------------------------------

type EncodedPtr = (Integer -> [Word8], [Word8])

-- Given the list of requested files and the list of all files including transitive imports,
-- returns a tuple containing the appropriate encoded CodeGeneratorRequest as well as a list
-- of ((typeId, displayName), encodedNode), where encodedNode is the encoded schema node
-- appropriate for reading as a "trusted message".
encodeSchema :: [FileDesc] -> [FileDesc] -> ([Word8], [((Word64, String), [Word8])])
encodeSchema requestedFiles allFiles = (encRoot, nodesForEmbedding) where
    encUInt64 = EncodedBytes . flip intToBytes 8
    encUInt32 = EncodedBytes . flip intToBytes 4
    encUInt16 :: (Integral a, Bits a) => a -> EncodedData
    encUInt16 = EncodedBytes . flip intToBytes 2
    encText :: String -> EncodedPtr
    encText v = encodePointerValue (BuiltinType BuiltinText) (TextDesc v)

    encDataList :: DataSize -> [EncodedData] -> EncodedPtr
    encDataList elementSize elements = let
        elemBits = dataSizeInBits elementSize
        bytes = packBytes (elemBits * genericLength elements)
              $ zip [0,elemBits..] elements
        in (encodeListPointer (SizeData elementSize) (genericLength elements), bytes)

    -- Not used, but maybe useful in the future.
    --encPtrList :: [EncodedPtr] -> EncodedPtr
    --encPtrList elements = let
    --    (ptrBytes, childBytes) = packPointers (genericLength elements) (zip [0..] elements) 0
    --    in (encodeListPointer SizePointer (genericLength elements), ptrBytes ++ childBytes)

    encStructList :: (DataSectionSize, Integer)
                  -> [([(Integer, EncodedData)], [(Integer, EncodedPtr)])]
                  -> EncodedPtr
    encStructList elementSize@(dataSize, pointerCount) elements = let
        count = genericLength elements
        tag = encodeStructPointer elementSize count
        eSize = dataSectionWordSize dataSize + pointerCount
        (elemBytes, childBytes) = loop (eSize * genericLength elements) elements
        loop _ [] = ([], [])
        loop offset ((dataValues, ptrValues):rest) = let
            offsetFromElementEnd = offset - eSize
            (dataBytes, ptrBytes, childBytes2) =
                encStructBody elementSize dataValues ptrValues offsetFromElementEnd
            childLen = genericLength childBytes2
            childWordLen = if mod childLen 8 == 0
                then div childLen 8
                else error "Child not word-aligned."
            (restBytes, restChildren) = loop (offsetFromElementEnd + childWordLen) rest
            in (concat [dataBytes, ptrBytes, restBytes], childBytes2 ++ restChildren)
        in (encodeListPointer (SizeInlineComposite dataSize pointerCount) (genericLength elements),
            concat [tag, elemBytes, childBytes])

    encStructBody :: (DataSectionSize, Integer)
                  -> [(Integer, EncodedData)]
                  -> [(Integer, EncodedPtr)]
                  -> Integer
                  -> ([Word8], [Word8], [Word8])
    encStructBody (dataSize, pointerCount) dataValues ptrValues offsetFromElementEnd = let
        dataBytes = packBytes (dataSectionBits dataSize) dataValues
        (ptrBytes, childBytes) = packPointers pointerCount ptrValues offsetFromElementEnd
        in (dataBytes, ptrBytes, childBytes)

    encStruct :: (DataSectionSize, Integer)
              -> ([(Integer, EncodedData)], [(Integer, EncodedPtr)])
              -> EncodedPtr
    encStruct size (dataValues, ptrValues) = let
        (dataBytes, ptrBytes, childBytes) = encStructBody size dataValues ptrValues 0
        in (encodeStructPointer size, concat [dataBytes, ptrBytes, childBytes])

    ---------------------------------------------

    isNodeDesc (DescFile _) = True
    isNodeDesc (DescStruct _) = True
    isNodeDesc (DescEnum _) = True
    isNodeDesc (DescInterface _) = True
    isNodeDesc (DescConstant _) = True
    isNodeDesc (DescAnnotation _) = True
    isNodeDesc _ = False

    descNestedNodes (DescFile      d) = filter isNodeDesc $ fileMembers d
    descNestedNodes (DescStruct    d) = filter isNodeDesc $ structMembers d
    descNestedNodes (DescInterface d) = filter isNodeDesc $ interfaceMembers d
    descNestedNodes _ = []

    flattenDescs desc = desc : concatMap flattenDescs (descNestedNodes desc)

    allDescs = concatMap flattenDescs $ map DescFile allFiles

    allNodes = map encNode allDescs

    nodesForEmbedding = map encodeNodeForEmbedding allNodes

    ---------------------------------------------

    encRoot = let
        ptrVal = encStruct codeGeneratorRequestSize encCodeGeneratorRequest
        (ptrBytes, childBytes) = packPointers 1 [(0, ptrVal)] 0
        segment = ptrBytes ++ childBytes
        in concat [[0,0,0,0], intToBytes (div (length segment) 8) 4, segment]

    encodeNodeForEmbedding ((typeId, name), node) = let
        ptrVal = encStruct nodeSize node
        (ptrBytes, childBytes) = packPointers 1 [(0, ptrVal)] 0
        in ((typeId, name), ptrBytes ++ childBytes)

    codeGeneratorRequestSize = (DataSectionWords 0, 2)
    encCodeGeneratorRequest = (dataValues, ptrValues) where
        dataValues = []
        ptrValues = [ (0, encStructList nodeSize $ map snd allNodes)
                    , (1, encDataList Size64 $ map (encUInt64 . fileId) requestedFiles)
                    ]

    typeSize = (DataSectionWords 2, 1)
    encType t = (dataValues, ptrValues) where
        dataValues = [ (0, encUInt16 discrim)
                     , (64, encUInt64 typeId)
                     ]
        ptrValues = case listElementType of
            Nothing -> []
            Just et -> [ (0, encStruct typeSize $ encType et) ]

        (discrim, typeId, listElementType) = case t of
            BuiltinType BuiltinVoid -> (0::Word16, 0, Nothing)
            BuiltinType BuiltinBool -> (1, 0, Nothing)
            BuiltinType BuiltinInt8 -> (2, 0, Nothing)
            BuiltinType BuiltinInt16 -> (3, 0, Nothing)
            BuiltinType BuiltinInt32 -> (4, 0, Nothing)
            BuiltinType BuiltinInt64 -> (5, 0, Nothing)
            BuiltinType BuiltinUInt8 -> (6, 0, Nothing)
            BuiltinType BuiltinUInt16 -> (7, 0, Nothing)
            BuiltinType BuiltinUInt32 -> (8, 0, Nothing)
            BuiltinType BuiltinUInt64 -> (9, 0, Nothing)
            BuiltinType BuiltinFloat32 -> (10, 0, Nothing)
            BuiltinType BuiltinFloat64 -> (11, 0, Nothing)
            BuiltinType BuiltinText -> (12, 0, Nothing)
            BuiltinType BuiltinData -> (13, 0, Nothing)
            BuiltinType BuiltinObject -> (18, 0, Nothing)
            ListType et -> (14, 0, Just et)
            EnumType d -> (15, enumId d, Nothing)
            StructType d -> (16, structId d, Nothing)
            InterfaceType d -> (17, interfaceId d, Nothing)
            InlineStructType _ -> error "Inline types not currently supported by codegen plugins."
            InlineListType _ _ -> error "Inline types not currently supported by codegen plugins."
            InlineDataType _ -> error "Inline types not currently supported by codegen plugins."

    valueSize = (DataSectionWords 2, 1)
    encValue t maybeValue = (dataValues, ptrValues) where
        dataValues = (0, encUInt16 discrim) : (case (maybeValue, fieldSize t) of
            (Nothing, _) -> []
            (_, SizeVoid) -> []
            (Just value, SizeData _) -> [ (64, encodeDataValue t value) ]
            (_, SizePointer) -> []
            (_, SizeInlineComposite _ _) ->
                error "Inline types not currently supported by codegen plugins.")
        ptrValues = case (maybeValue, fieldSize t) of
            (Nothing, _) -> []
            (_, SizeVoid) -> []
            (_, SizeData _) -> []
            (Just value, SizePointer) -> [ (0, encodePointerValue t value) ]
            (_, SizeInlineComposite _ _) ->
                error "Inline types not currently supported by codegen plugins."

        discrim = case t of
            BuiltinType BuiltinVoid -> 9::Word16
            BuiltinType BuiltinBool -> 1
            BuiltinType BuiltinInt8 -> 2
            BuiltinType BuiltinInt16 -> 3
            BuiltinType BuiltinInt32 -> 4
            BuiltinType BuiltinInt64 -> 5
            BuiltinType BuiltinUInt8 -> 6
            BuiltinType BuiltinUInt16 -> 7
            BuiltinType BuiltinUInt32 -> 8
            BuiltinType BuiltinUInt64 -> 0
            BuiltinType BuiltinFloat32 -> 10
            BuiltinType BuiltinFloat64 -> 11
            BuiltinType BuiltinText -> 12
            BuiltinType BuiltinData -> 13
            BuiltinType BuiltinObject -> 18
            ListType _ -> 14
            EnumType _ -> 15
            StructType _ -> 16
            InterfaceType _ -> 17
            InlineStructType _ -> error "Inline types not currently supported by codegen plugins."
            InlineListType _ _ -> error "Inline types not currently supported by codegen plugins."
            InlineDataType _ -> error "Inline types not currently supported by codegen plugins."

    annotationSize = (DataSectionWords 1, 1)
    encAnnotation (annId, (desc, value)) = (dataValues, ptrValues) where
        dataValues = [ (0, encUInt64 annId) ]
        ptrValues = [ (0, encStruct valueSize $ encValue (annotationType desc) (Just value)) ]

    encAnnotationList annotations =
        encStructList annotationSize $ map encAnnotation $ Map.toList annotations

    nodeSize = (DataSectionWords 3, 4)
    encNode :: Desc -> ((Word64, String), ([(Integer, EncodedData)], [(Integer, EncodedPtr)]))
    encNode desc = ((descId desc, dname), (dataValues, ptrValues)) where
        dataValues = [ (0, encUInt64 $ descId desc)
                     , (64, encUInt64 $ scopedId desc)
                     , (128, encUInt16 discrim)
                     ]
        ptrValues = [ (0, encText dname)
                    , (1, encStructList nestedNodeSize $ map encNestedNode $ descNestedNodes desc)
                    , (2, encAnnotationList $ descAnnotations desc)
                    , (3, encStruct bodySize body)
                    ]

        dname = displayName desc

        (discrim, bodySize, body) = case desc of
            DescFile d -> (0::Word16, fileNodeSize, encFileNode d)
            DescStruct d -> (1, structNodeSize, encStructNode d)
            DescEnum d -> (2, enumNodeSize, encEnumNode d)
            DescInterface d -> (3, interfaceNodeSize, encInterfaceNode d)
            DescConstant d -> (4, constNodeSize, encConstNode d)
            DescAnnotation d -> (5, annotationNodeSize, encAnnotationNode d)
            _ -> error "Not a node type."

    displayName (DescFile f) = fileName f
    displayName desc = concat [fileName (descFile desc), ":", descName desc]

    nestedNodeSize = (DataSectionWords 1, 1)
    encNestedNode desc = (dataValues, ptrValues) where
        dataValues = [ (0, encUInt64 $ descId desc) ]
        ptrValues = [ (0, encText $ descName desc) ]

    scopedId (DescFile _) = 0
    scopedId desc = descId $ descParent desc

    fileNodeSize = (DataSectionWords 0, 1)
    encFileNode desc = (dataValues, ptrValues) where
        dataValues = []
        ptrValues = [ (0, encStructList importSize $ map encImport $ Map.toList $ fileImportMap desc) ]

        importSize = (DataSectionWords 1, 1)
        encImport (impName, impFile) = (dataValues2, ptrValues2) where
            dataValues2 = [ (0, encUInt64 $ fileId impFile) ]
            ptrValues2 = [ (0, encText impName) ]

    structNodeSize = (DataSectionWords 1, 1)
    encStructNode desc = (dataValues, ptrValues) where
        dataValues = [ (0, encUInt16 $ dataSectionWordSize $ structDataSize desc)
                     , (16, encUInt16 $ structPointerCount desc)
                     , (32, encUInt16 (fieldSizeEnum preferredListEncoding::Word16))
                     ]
        ptrValues = [ (0, encStructList memberSize $ map encMember $
                              sortMembers $ structMembers desc) ]

        preferredListEncoding = case (structDataSize desc, structPointerCount desc) of
            (DataSectionWords 0, 0) -> SizeVoid
            (DataSectionWords 0, 1) -> SizePointer
            (DataSection1, 0) -> SizeData Size1
            (DataSection8, 0) -> SizeData Size8
            (DataSection16, 0) -> SizeData Size16
            (DataSection32, 0) -> SizeData Size32
            (DataSectionWords 1, 0) -> SizeData Size64
            (ds, pc) -> SizeInlineComposite ds pc

        -- Extract just the field and union members, annotate them with ordinals and code order,
        -- and then sort by ordinal.
        sortMembers members = sortBy (compare `on` (fst . snd)) $ zip [0::Word16 ..]
                            $ mapMaybe selectFieldOrUnion members
        selectFieldOrUnion d@(DescField f) = Just (fieldNumber f, d)
        selectFieldOrUnion d@(DescUnion u) = Just (unionNumber u, d)
        selectFieldOrUnion _ = Nothing

        memberSize = (DataSectionWords 1, 3)
        encMember (codeOrder, (_, DescField field)) = (dataValues2, ptrValues2) where
            dataValues2 = [ (0, encUInt16 $ fieldNumber field)
                          , (16, encUInt16 codeOrder)
                          , (32, encUInt16 (0::Word16))  -- discriminant
                          ]
            ptrValues2 = [ (0, encText $ fieldName field)
                         , (1, encAnnotationList $ fieldAnnotations field)
                         , (2, encStruct (DataSection32, 2) (dataValues3, ptrValues3))
                         ]

            -- StructNode.Field
            dataValues3 = [ (0, encUInt32 $ offsetToInt $ fieldOffset field) ]
            ptrValues3 = [ (0, encStruct typeSize $ encType $ fieldType field)
                         , (1, encStruct valueSize $ encValue (fieldType field) $
                                   fieldDefaultValue field)
                         ]

            offsetToInt VoidOffset = 0
            offsetToInt (DataOffset _ i) = i
            offsetToInt (PointerOffset i) = i
            offsetToInt (InlineCompositeOffset {}) =
                error "Inline types not currently supported by codegen plugins."

        encMember (codeOrder, (_, DescUnion union)) = (dataValues2, ptrValues2) where
            dataValues2 = [ (0, encUInt16 $ unionNumber union)
                          , (16, encUInt16 codeOrder)
                          , (32, encUInt16 (1::Word16))  -- discriminant
                          ]
            ptrValues2 = [ (0, encText $ unionName union)
                         , (1, encAnnotationList $ unionAnnotations union)
                         , (2, encStruct (DataSection32, 1) (dataValues3, ptrValues3))
                         ]

            -- StructNode.Union
            dataValues3 = [ (0, encUInt32 $ unionTagOffset union) ]
            ptrValues3 = [ (0, encStructList memberSize $ map encMember $ sortMembers $
                                   unionMembers union) ]
        encMember _ = error "Not a field or union?"

    enumNodeSize = (DataSectionWords 0, 1)
    encEnumNode desc = (dataValues, ptrValues) where
        dataValues = []
        ptrValues = [ (0, encStructList enumerantSize $ map encEnumerant sortedEnumerants) ]

        sortedEnumerants = sortBy (compare `on` (enumerantNumber . snd))
                         $ zip [0::Word16 ..] $ enumerants desc

        enumerantSize = (DataSection16, 2)
        encEnumerant (codeOrder, enumerant) = (dataValues2, ptrValues2) where
            dataValues2 = [ (0, encUInt16 codeOrder) ]
            ptrValues2 = [ (0, encText $ enumerantName enumerant)
                         , (1, encAnnotationList $ enumerantAnnotations enumerant)
                         ]

    interfaceNodeSize = (DataSectionWords 0, 1)
    encInterfaceNode desc = (dataValues, ptrValues) where
        dataValues = []
        ptrValues = [ (0, encStructList methodSize $ map encMethod sortedMethods) ]

        sortedMethods = sortBy (compare `on` (methodNumber . snd))
                      $ zip [0::Word16 ..] $ interfaceMethods desc

        methodSize = (DataSection32, 4)
        encMethod (codeOrder, method) = (dataValues2, ptrValues2) where
            dataValues2 = [ (0, encUInt16 codeOrder)
                          , (16, encUInt16 requiredParamCount) ]
            ptrValues2 = [ (0, encText $ methodName method)
                         , (1, encStructList paramSize $ map encParam $ methodParams method)
                         , (2, encStruct typeSize $ encType $ methodReturnType method)
                         , (3, encAnnotationList $ methodAnnotations method)
                         ]

            paramIndicesWithoutDefaults =
                findIndices (isNothing . paramDefaultValue) $ methodParams method
            requiredParamCount = maybe 0 (+1) $ listToMaybe
                               $ reverse paramIndicesWithoutDefaults

        paramSize = (DataSectionWords 0, 4)
        encParam param = (dataValues2, ptrValues2) where
            dataValues2 = []
            ptrValues2 = [ (0, encText $ paramName param)
                         , (1, encStruct typeSize $ encType $ paramType param)
                         , (2, encStruct valueSize $ encValue (paramType param) $
                                   paramDefaultValue param)
                         , (3, encAnnotationList $ paramAnnotations param)
                         ]

    constNodeSize = (DataSectionWords 0, 2)
    encConstNode desc = (dataValues, ptrValues) where
        dataValues = []
        ptrValues = [ (0, encStruct typeSize $ encType $ constantType desc)
                    , (1, encStruct valueSize $ encValue (constantType desc) $ Just $
                              constantValue desc)
                    ]

    annotationNodeSize = (DataSection16, 1)
    encAnnotationNode desc = (dataValues, ptrValues) where
        dataValues = [ (0, encTarget FileAnnotation)
                     , (1, encTarget ConstantAnnotation)
                     , (2, encTarget EnumAnnotation)
                     , (3, encTarget EnumerantAnnotation)
                     , (4, encTarget StructAnnotation)
                     , (5, encTarget FieldAnnotation)
                     , (6, encTarget UnionAnnotation)
                     , (7, encTarget InterfaceAnnotation)
                     , (8, encTarget MethodAnnotation)
                     , (9, encTarget ParamAnnotation)
                     , (10, encTarget AnnotationAnnotation)
                     ]
        ptrValues = [ (0, encStruct typeSize $ encType $ annotationType desc) ]

        encTarget t = EncodedBit $ Set.member t $ annotationTargets desc
