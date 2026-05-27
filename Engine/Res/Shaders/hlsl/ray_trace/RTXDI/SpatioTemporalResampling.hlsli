// /*
//  * SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
//  * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
//  *
//  * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
//  * property and proprietary rights in and to this material, related
//  * documentation and any modifications thereto. Any use, reproduction,
//  * disclosure or distribution of this material and related documentation
//  * without an express license agreement from NVIDIA CORPORATION or
//  * its affiliates is strictly prohibited.
//  */

// #ifndef RTXDI_DI_SPATIOTEMPORAL_RESAMPLIHG_HLSLI
// #define RTXDI_DI_SPATIOTEMPORAL_RESAMPLIHG_HLSLI

// //#include "Rtxdi/DI/PairwiseStreaming.hlsli"
// #include "Reservoir.hlsli"
// #include "RAB_Struct.hlsli"
// #include "CommonStruct.hlsli"
// //#include <Rtxdi/DI/ReservoirStorage.hlsli>
// //#include "Rtxdi/Utils/Checkerboard.hlsli"


// #define RTXDI_ALLOWED_BIAS_CORRECTION 0
// // This macro can be defined in the including shader file to reduce code bloat
// // and/or remove ray tracing calls from temporal and spatial resampling shaders
// // if bias correction is not necessary.
// #ifndef RTXDI_ALLOWED_BIAS_CORRECTION
// #define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
// #endif


// // Spatio-temporal resampling pass.
// // A combination of the temporal and spatial passes that operates only on the previous frame reservoirs.
// // The selectedLightSample parameter is used to update and return the selected sample; it's optional,
// // and it's safe to pass a null structure there and ignore the result.
// RTXDI_DIReservoir RTXDI_DISpatioTemporalResampling(
//     uint2 pixelPosition,
//     RAB_Surface surface,
//     RTXDI_DIReservoir curSample,
//     inout RTXDI_RandomSamplerState rng,
//     float3 screenSpaceMotion,
//     uint sourceBufferIndex,
//     RTXDI_RuntimeParameters params,
//     RTXDI_ReservoirBufferParameters reservoirParams,
//     RTXDI_DISpatioTemporalResamplingParameters stparams,
//     out int2 temporalSamplePixelPos,
//     inout RAB_LightSample selectedLightSample)
// {
//     // if (stparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_PAIRWISE)
//     // {
//     //     return RTXDI_DISpatioTemporalResamplingWithPairwiseMIS(pixelPosition, surface,
//     //         curSample, rng, screenSpaceMotion, sourceBufferIndex, params, reservoirParams, stparams, temporalSamplePixelPos, selectedLightSample);
//     // }

//     uint historyLimit = min(RTXDI_PackedDIReservoir_MaxM, uint(stparams.maxHistoryLength * curSample.M));

//     int selectedLightPrevID = -1;

//     if (RTXDI_IsValidDIReservoir(curSample))
//     {
//         selectedLightPrevID = RAB_TranslateLightIndex(RTXDI_GetDIReservoirLightIndex(curSample), true);
//     }

//     temporalSamplePixelPos = int2(-1, -1);

//     RTXDI_DIReservoir state = RTXDI_EmptyDIReservoir();
//     RTXDI_CombineDIReservoirs(state, curSample, /* random = */ 0.5, curSample.targetPdf);

//     uint startIdx = uint(RTXDI_GetNextRandom(rng) * params.neighborOffsetMask);

//     // Backproject this pixel to last frame
//     float3 motion = screenSpaceMotion;

//     if (!stparams.enablePermutationSampling)
//     {
//         motion.xy += float2(RTXDI_GetNextRandom(rng), RTXDI_GetNextRandom(rng)) - 0.5;
//     }

//     float2 reprojectedSamplePosition = float2(pixelPosition) + motion.xy;
//     int2 prevPos = int2(round(reprojectedSamplePosition));

//     float expectedPrevLinearDepth = RAB_GetSurfaceLinearDepth(surface) + motion.z;

//     int i;

//     RAB_Surface temporalSurface = RAB_EmptySurface();
//     bool foundTemporalSurface = false;
//     const float temporalSearchRadius = (params.activeCheckerboardField == 0) ? 4 : 8;
//     int2 temporalSpatialOffset = int2(0, 0);

//     // Try to find a matching surface in the neighborhood of the reprojected pixel
//     for (i = 0; i < 9; i++)
//     {
//         int2 offset = int2(0, 0);
//         if (i > 0)
//         {
//             offset.x = int((RTXDI_GetNextRandom(rng) - 0.5) * temporalSearchRadius);
//             offset.y = int((RTXDI_GetNextRandom(rng) - 0.5) * temporalSearchRadius);
//         }

//         int2 idx = prevPos + offset;

//         if (stparams.enablePermutationSampling && i == 0)
//         {
//             RTXDI_ApplyPermutationSampling(idx, stparams.uniformRandomNumber);
//         }

//         RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);

//         // Grab shading / g-buffer data from last frame
//         temporalSurface = RAB_GetGBufferSurface(idx, true);
//         if (!RAB_IsSurfaceValid(temporalSurface))
//             continue;
        
//         // Test surface similarity, discard the sample if the surface is too different.
//         if (!RTXDI_IsValidNeighbor(
//             RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface), 
//             expectedPrevLinearDepth, RAB_GetSurfaceLinearDepth(temporalSurface), 
//             stparams.normalThreshold, stparams.depthThreshold))
//             continue;

//         temporalSpatialOffset = idx - prevPos;
//         foundTemporalSurface = true;
//         break;
//     }

//     // Clamp the sample count at 32 to make sure we can keep the neighbor mask in an uint (cachedResult)
//     uint numSamples = clamp(stparams.numSamples, 1, 32);

//     // Apply disocclusion boost if there is no temporal surface
//     if (!foundTemporalSurface)
//         numSamples = clamp(stparams.numDisocclusionBoostSamples, numSamples, 32);

//     // We loop through neighbors twice.  Cache the validity / edge-stopping function
//     //   results for the 2nd time through.
//     uint cachedResult = 0;

//     // Since we're using our bias correction scheme, we need to remember which light selection we made
//     int selected = -1;

//     // Walk the specified number of neighbors, resampling using RIS
//     for (i = 0; i < numSamples; ++i)
//     {
//         int2 spatialOffset, idx;

//         // Get screen-space location of neighbor
//         if (i == 0 && foundTemporalSurface)
//         {
//             spatialOffset = temporalSpatialOffset;
//             idx = prevPos + spatialOffset;
//         }
//         else
//         {
//             uint sampleIdx = (startIdx + i) & params.neighborOffsetMask;
//             spatialOffset = int2(float2(RTXDI_NEIGHBOR_OFFSETS_BUFFER[sampleIdx].xy) * stparams.samplingRadius);

//             idx = prevPos + spatialOffset;

//             idx = RAB_ClampSamplePositionIntoView(idx, true);

//             RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);

//             temporalSurface = RAB_GetGBufferSurface(idx, true);

//             if (!RAB_IsSurfaceValid(temporalSurface))
//                 continue;

//             if (!RTXDI_IsValidNeighbor(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceNormal(temporalSurface), 
//                 RAB_GetSurfaceLinearDepth(surface), RAB_GetSurfaceLinearDepth(temporalSurface), 
//                 stparams.normalThreshold, stparams.depthThreshold))
//                 continue;

//             if (stparams.enableMaterialSimilarityTest && !RAB_AreMaterialsSimilar(RAB_GetMaterial(surface), RAB_GetMaterial(temporalSurface)))
//                 continue;
//         }
        
//         cachedResult |= (1u << uint(i));

//         uint2 neighborReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);

//         RTXDI_DIReservoir prevSample = RTXDI_LoadDIReservoir(reservoirParams,
//             neighborReservoirPos, sourceBufferIndex);

//         if (RTXDI_IsValidDIReservoir(prevSample))
//         {
//             if (stparams.discountNaiveSamples && prevSample.M <= RTXDI_NAIVE_SAMPLING_M_THRESHOLD)
//                 continue;
//         }

//         prevSample.M = min(prevSample.M, historyLimit);
//         prevSample.spatialDistance += spatialOffset;
//         prevSample.age += 1;

//         uint originalPrevLightID = RTXDI_GetDIReservoirLightIndex(prevSample);

//         // Map the light ID from the previous frame into the current frame, if it still exists
//         if (RTXDI_IsValidDIReservoir(prevSample))
//         {   
//             if (i == 0 && foundTemporalSurface && prevSample.age <= 1)
//             {
//                 temporalSamplePixelPos = idx;
//             }

//             int mappedLightID = RAB_TranslateLightIndex(RTXDI_GetDIReservoirLightIndex(prevSample), false);

//             if (mappedLightID < 0)
//             {
//                 // Kill the reservoir
//                 prevSample.weightSum = 0;
//                 prevSample.lightData = 0;
//             }
//             else
//             {
//                 // Sample is valid - modify the light ID stored
//                 prevSample.lightData = mappedLightID | RTXDI_DIReservoir_LightValidBit;
//             }
//         }

//         RAB_LightInfo candidateLight;

//         // Load that neighbor's RIS state, do resampling
//         float neighborWeight = 0;
//         RAB_LightSample candidateLightSample = RAB_EmptyLightSample();
//         if (RTXDI_IsValidDIReservoir(prevSample))
//         {   
//             candidateLight = RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(prevSample), false);
            
//             candidateLightSample = RAB_SamplePolymorphicLight(
//                 candidateLight, surface, RTXDI_GetDIReservoirSampleUV(prevSample));
            
//             neighborWeight = RAB_GetLightSampleTargetPdfForSurface(candidateLightSample, surface);
//         }

//         if (RTXDI_CombineDIReservoirs(state, prevSample, RTXDI_GetNextRandom(rng), neighborWeight))
//         {
//             selected = i;
//             selectedLightPrevID = int(originalPrevLightID);
//             selectedLightSample = candidateLightSample;
//         }
//     }

//     if (RTXDI_IsValidDIReservoir(state))
//     {
// #if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_BASIC
//         if (stparams.biasCorrectionMode >= RTXDI_BIAS_CORRECTION_BASIC)
//         {
//             // Compute the unbiased normalization term (instead of using 1/M)
//             float pi = state.targetPdf;
//             float piSum = state.targetPdf * curSample.M;

//             if (selectedLightPrevID >= 0)
//             {
//                 const RAB_LightInfo selectedLightPrev = RAB_LoadLightInfo(selectedLightPrevID, true);

//                 // To do this, we need to walk our neighbors again
//                 for (i = 0; i < numSamples; ++i)
//                 {
//                     // If we skipped this neighbor above, do so again.
//                     if ((cachedResult & (1u << uint(i))) == 0) continue;

//                     uint sampleIdx = (startIdx + i) & params.neighborOffsetMask;

//                     // Get the screen-space location of our neighbor
//                     int2 spatialOffset = (i == 0 && foundTemporalSurface) 
//                         ? temporalSpatialOffset 
//                         : int2(float2(RTXDI_NEIGHBOR_OFFSETS_BUFFER[sampleIdx].xy) * stparams.samplingRadius);
//                     int2 idx = prevPos + spatialOffset;

//                     if (!(i == 0 && foundTemporalSurface))
//                     {
//                         idx = RAB_ClampSamplePositionIntoView(idx, true);
//                     }

//                     RTXDI_ActivateCheckerboardPixel(idx, true, params.activeCheckerboardField);

//                     // Load our neighbor's G-buffer
//                     RAB_Surface neighborSurface = RAB_GetGBufferSurface(idx, true);
                    
//                     // Get the PDF of the sample RIS selected in the first loop, above, *at this neighbor* 
//                     const RAB_LightSample selectedSampleAtNeighbor = RAB_SamplePolymorphicLight(
//                         selectedLightPrev, neighborSurface, RTXDI_GetDIReservoirSampleUV(state));

//                     float ps = RAB_GetLightSampleTargetPdfForSurface(selectedSampleAtNeighbor, neighborSurface);

// #if RTXDI_ALLOWED_BIAS_CORRECTION >= RTXDI_BIAS_CORRECTION_RAY_TRACED
//                                                                                                               // TODO:  WHY?
//                     if (stparams.biasCorrectionMode == RTXDI_BIAS_CORRECTION_RAY_TRACED && ps > 0 && (selected != i || i != 0 || !stparams.enableVisibilityShortcut))
//                     {
//                         RAB_Surface fallbackSurface;
//                         if (i == 0 && foundTemporalSurface)
//                             fallbackSurface = surface;
//                         else
//                             fallbackSurface = neighborSurface;

//                         if (!RAB_GetTemporalConservativeVisibility(fallbackSurface, neighborSurface, selectedSampleAtNeighbor))
//                         {
//                             ps = 0;
//                         }
//                     }
// #endif

//                     uint2 neighborReservoirPos = RTXDI_PixelPosToReservoirPos(idx, params.activeCheckerboardField);

//                     RTXDI_DIReservoir prevSample = RTXDI_LoadDIReservoir(reservoirParams,
//                         neighborReservoirPos, sourceBufferIndex);
//                     prevSample.M = min(prevSample.M, historyLimit);

//                     // Select this sample for the (normalization) numerator if this particular neighbor pixel
//                     //     was the one we selected via RIS in the first loop, above.
//                     pi = selected == i ? ps : pi;

//                     // Add to the sums of weights for the (normalization) denominator
//                     piSum += ps * prevSample.M;
//                 }
//             }

//             // Use "MIS-like" normalization
//             RTXDI_FinalizeResampling(state, pi, piSum);
//         }
//         else
// #endif
//         {
//             RTXDI_FinalizeResampling(state, 1.0, state.M);
//         }
//     }

//     return state;
// }

// #endif // RTXDI_DI_SPATIOTEMPORAL_RESAMPLIHG_HLSLI
