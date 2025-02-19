/*
 * Copyright (C) 2024 Amlogic Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef _AML_DTS_CHMASK_TABLE_H_
#define _AML_DTS_CHMASK_TABLE_H_

#define AML_DTS_SPEAKER_MAX_SPEAKERS    (32)

typedef enum
{
    AML_DTS_MASK_SPEAKER_CENTER = 0x00000001, /**< Center */
    AML_DTS_MASK_SPEAKER_LEFT = 0x00000002, /**< Left */
    AML_DTS_MASK_SPEAKER_RIGHT = 0x00000004, /**< Right */
    AML_DTS_MASK_SPEAKER_LS = 0x00000008, /**< Left Surround */
    AML_DTS_MASK_SPEAKER_RS = 0x00000010, /**< Right Surround */
    AML_DTS_MASK_SPEAKER_LFE1 = 0x00000020, /**< Low Frequency Effects 1 */
    AML_DTS_MASK_SPEAKER_Cs = 0x00000040, /**< Center Surround */
    AML_DTS_MASK_SPEAKER_Lsr = 0x00000080, /**< Left Surround in Rear */
    AML_DTS_MASK_SPEAKER_Rsr = 0x00000100, /**< Right Surround in Rear */
    AML_DTS_MASK_SPEAKER_Lss = 0x00000200, /**< Left Surround on Side */
    AML_DTS_MASK_SPEAKER_Rss = 0x00000400, /**< Right Surround on Side */
    AML_DTS_MASK_SPEAKER_Lc = 0x00000800, /**< Between Left and Center in front */
    AML_DTS_MASK_SPEAKER_Rc = 0x00001000, /**< Between Right and Center in front */
    AML_DTS_MASK_SPEAKER_Lh = 0x00002000, /**< Left Height in front */
    AML_DTS_MASK_SPEAKER_Ch = 0x00004000, /**< Center Height in Front */
    AML_DTS_MASK_SPEAKER_Rh = 0x00008000, /**< Right Height in front */
    AML_DTS_MASK_SPEAKER_LFE2 = 0x00010000, /**< Low Frequency Effects 2 */
    AML_DTS_MASK_SPEAKER_Lw = 0x00020000, /**< Left on side in front */
    AML_DTS_MASK_SPEAKER_Rw = 0x00040000, /**< Right on side in front */
    AML_DTS_MASK_SPEAKER_Oh = 0x00080000, /**< Over the listeners Head */
    AML_DTS_MASK_SPEAKER_Lhs = 0x00100000, /**< Left Height on Side */
    AML_DTS_MASK_SPEAKER_Rhs = 0x00200000, /**< Right Height on Side */
    AML_DTS_MASK_SPEAKER_Chr = 0x00400000, /**< Center Height in Rear */
    AML_DTS_MASK_SPEAKER_Lhr = 0x00800000, /**< Left Height in Rear */
    AML_DTS_MASK_SPEAKER_Rhr = 0x01000000, /**< Right Height in Rear */
    AML_DTS_MASK_SPEAKER_Clf = 0x02000000, /**< Low Center in Front */
    AML_DTS_MASK_SPEAKER_Llf = 0x04000000, /**< Low Left in Front */
    AML_DTS_MASK_SPEAKER_Rlf = 0x08000000, /**< Low Right in Front */
    AML_DTS_MASK_SPEAKER_Ltf = 0x10000000, /**< Top Left in Front */
    AML_DTS_MASK_SPEAKER_Rtf = 0x20000000, /**< Top Right in Front */
    AML_DTS_MASK_SPEAKER_Ltr = 0x40000000, /**< Top Left in Rear */
    AML_DTS_MASK_SPEAKER_Rtr = 0x80000000, /**< Top Right in Rear */
} AmlDtsSpeakerMask;

#define AML_DTS_CHANNEL_MASK_2_0 \
(AML_DTS_MASK_SPEAKER_LEFT|AML_DTS_MASK_SPEAKER_RIGHT)  // 0x6

#define AML_DTS_CHANNEL_MASK_5_1 \
(AML_DTS_MASK_SPEAKER_CENTER|AML_DTS_MASK_SPEAKER_LFE1\
|AML_DTS_MASK_SPEAKER_LEFT|AML_DTS_MASK_SPEAKER_RIGHT\
|AML_DTS_MASK_SPEAKER_LS|AML_DTS_MASK_SPEAKER_RS)   // 0x3F

#define AML_DTS_CHANNEL_MASK_5_1_2 \
(AML_DTS_MASK_SPEAKER_CENTER|AML_DTS_MASK_SPEAKER_LFE1\
|AML_DTS_MASK_SPEAKER_LEFT|AML_DTS_MASK_SPEAKER_RIGHT\
|AML_DTS_MASK_SPEAKER_LS|AML_DTS_MASK_SPEAKER_RS\
|AML_DTS_MASK_SPEAKER_Lh|AML_DTS_MASK_SPEAKER_Rh)   // 0xA03F

#define AML_DTS_CHANNEL_MASK_7_1 \
(AML_DTS_MASK_SPEAKER_CENTER|AML_DTS_MASK_SPEAKER_LFE1\
|AML_DTS_MASK_SPEAKER_LEFT|AML_DTS_MASK_SPEAKER_RIGHT\
|AML_DTS_MASK_SPEAKER_Lsr|AML_DTS_MASK_SPEAKER_Rsr\
|AML_DTS_MASK_SPEAKER_Lss|AML_DTS_MASK_SPEAKER_Rss) // 0x7A7

#define AML_DTS_CHANNEL_MASK_5_1_4 \
(AML_DTS_MASK_SPEAKER_CENTER|AML_DTS_MASK_SPEAKER_LFE1\
|AML_DTS_MASK_SPEAKER_LEFT|AML_DTS_MASK_SPEAKER_RIGHT\
|AML_DTS_MASK_SPEAKER_LS|AML_DTS_MASK_SPEAKER_RS\
|AML_DTS_MASK_SPEAKER_Lh|AML_DTS_MASK_SPEAKER_Rh\
|AML_DTS_MASK_SPEAKER_Lhr|AML_DTS_MASK_SPEAKER_Rhr) // 0x180A03F

#define AML_DTS_CHANNEL_MASK_7_1_4 \
(AML_DTS_MASK_SPEAKER_CENTER|AML_DTS_MASK_SPEAKER_LFE1\
|AML_DTS_MASK_SPEAKER_LEFT|AML_DTS_MASK_SPEAKER_RIGHT\
|AML_DTS_MASK_SPEAKER_Lss|AML_DTS_MASK_SPEAKER_Rss\
|AML_DTS_MASK_SPEAKER_Lsr|AML_DTS_MASK_SPEAKER_Rsr\
|AML_DTS_MASK_SPEAKER_Lh|AML_DTS_MASK_SPEAKER_Rh\
|AML_DTS_MASK_SPEAKER_Lhr|AML_DTS_MASK_SPEAKER_Rhr) // 0x180A7A7

#endif /* _AML_DTS_CHMASK_TABLE_H_ */