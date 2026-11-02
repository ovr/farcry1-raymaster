// SPDX-License-Identifier: GPL-3.0-or-later WITH AdditionRef-FarCry-exception
// Please do not use this file to train or fine-tune AI models (see README.md).
//////////////////////////////////////////////////////////////////////
//
//	File: TangentFrame.h
//	Description: The tangent frames of a triangle list (bump mapping:
//	CLeafBuffer::CreateTangBuffer; the unit tests). Per vertex:
//	  - the tangent and the binormal from its triangles' texture
//	    coordinates (summed over the triangles that use the vertex);
//	  - the normal: the vertex's own (the mesh's) while it fits its
//	    triangles, within NORMAL_FIT_COS of their normal; else, and
//	    without one, the normal of its triangles. A mesh's normals need
//	    not fit its faces: the game's meshes carry one normal per
//	    position, and a box exported without split vertices at its hard
//	    edges (the shelves of Training's store, shelve_indoor_*.cgf) has
//	    the normal of one side on the corners of the other, at right
//	    angles to them. A frame with that normal puts the normal map's
//	    normal in the surface's plane, and the path tracer, which turns it
//	    to the side of the triangle the camera sees, turned it up and
//	    down from a pixel to the next (PathGBufferNormal). The SDK's
//	    tangent space calculation (TangentSpaceCalculation.h, the
//	    engine's) takes the frame's normal from the triangles too, summed
//	    by the angle at the vertex;
//	  - the tangent made perpendicular to the normal, the binormal the
//	    normal x the tangent on the side of the texture's v.
//	Without the engine's types: positions, texture coordinates and
//	normals are read from strided arrays.
//
//////////////////////////////////////////////////////////////////////

#ifndef XRENDERD3D12_TANGENTFRAME_H
#define XRENDERD3D12_TANGENTFRAME_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace TangentFrame
{
  //! One vertex's frame: the tangent, the binormal and the normal, unit vectors.
  struct SFrame
  {
    float T[3], B[3], N[3];
  };

  //! A vertex's own normal is the frame's while its cosine to the normal of its triangles is at least this (60 degrees).
  //! A smooth mesh's normals are within a few degrees of their faces', a hard edge's shared normal (the sides' mean)
  //! within 45 (55 at a box's corner); the normals of another side of a box are at 90.
  constexpr float NORMAL_FIT_COS = 0.5f;

  //! The triangles' normal is a side only when their unit normals, summed by angle, keep at least this share of the sum
  //! of the angles: the two sides of a sheet whose triangles share the vertices cancel out, and its own normal stays.
  constexpr float FACES_AGREE = 0.1f;

  //! Where a mesh's vertices are: nVerts records nStride bytes apart, the position (3 floats) at byte nPos, the texture
  //! coordinates (2 floats) at nTC (< 0: none, all 0); the vertices' own normals (3 floats) nNormalStride bytes apart
  //! from pNormals for the first nNormals vertices (pNormals null or nNormals 0: none).
  struct SMesh
  {
    const uint8_t *pVerts = nullptr;
    size_t nStride = 0;
    int nPos = 0;
    int nTC = -1;
    const uint8_t *pNormals = nullptr;
    size_t nNormalStride = 12;
    int nNormals = 0;
  };

  inline void Cross(const float *a, const float *b, float *r)
  {
    r[0] = a[1] * b[2] - a[2] * b[1];
    r[1] = a[2] * b[0] - a[0] * b[2];
    r[2] = a[0] * b[1] - a[1] * b[0];
  }

  //! Makes v a unit vector when it is longer than 1e-12; returns its length.
  inline float Normalize3(float *v)
  {
    const float f = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (f > 1e-12f)
    {
      v[0] /= f;
      v[1] /= f;
      v[2] /= f;
    }
    return f;
  }

  inline float Dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

  //! The angle (radians) between the edges a and b of a triangle at their common vertex; 0 for an edge of no length.
  inline float EdgeAngle(const float *a, const float *b)
  {
    const float la = sqrtf(Dot3(a, a)), lb = sqrtf(Dot3(b, b));
    if (la <= 1e-12f || lb <= 1e-12f)
      return 0.f;
    float c = Dot3(a, b) / (la * lb);
    c = c < -1.f ? -1.f : (c > 1.f ? 1.f : c);
    return acosf(c);
  }

  //! The frames of the mesh's nVerts vertices (pOut) from the triangle list pTris (nIndices indices; triangles with an
  //! index out of range are skipped).
  inline void Build(const SMesh &m, int nVerts, const uint16_t *pTris, size_t nIndices, SFrame *pOut)
  {
    if (nVerts <= 0 || !m.pVerts || !pOut)
      return;
    // Per vertex: the tangent, the binormal and the face normal summed (by the triangles' size), then the unit face
    // normals summed by the angle at the vertex, and the angles.
    std::vector<float> Acc((size_t)nVerts * 9, 0.f);
    std::vector<float> Faces((size_t)nVerts * 4, 0.f);
    const auto Pos = [&](int i) { return (const float *)(m.pVerts + (size_t)i * m.nStride + m.nPos); };
    for (size_t t = 0; pTris && t + 2 < nIndices; t += 3)
    {
      const int i0 = pTris[t], i1 = pTris[t + 1], i2 = pTris[t + 2];
      if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts)
        continue;
      const float *p0 = Pos(i0), *p1 = Pos(i1), *p2 = Pos(i2);
      float uv[3][2] = {};
      if (m.nTC >= 0)
      {
        memcpy(uv[0], m.pVerts + (size_t)i0 * m.nStride + m.nTC, 8);
        memcpy(uv[1], m.pVerts + (size_t)i1 * m.nStride + m.nTC, 8);
        memcpy(uv[2], m.pVerts + (size_t)i2 * m.nStride + m.nTC, 8);
      }
      const float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
      const float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
      const float du1 = uv[1][0] - uv[0][0], dv1 = uv[1][1] - uv[0][1];
      const float du2 = uv[2][0] - uv[0][0], dv2 = uv[2][1] - uv[0][1];
      const float fDet = du1 * dv2 - du2 * dv1;
      const float r = fabsf(fDet) > 1e-12f ? 1.f / fDet : 0.f;
      float T[3], B[3], N[3];
      for (int k = 0; k < 3; k++)
      {
        T[k] = (e1[k] * dv2 - e2[k] * dv1) * r;
        B[k] = (e2[k] * du1 - e1[k] * du2) * r;
      }
      Cross(e1, e2, N);
      float U[3] = { N[0], N[1], N[2] };
      const bool bFace = Normalize3(U) > 1e-12f;
      const float e3[3] = { p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2] };
      const float n1[3] = { -e1[0], -e1[1], -e1[2] }, n2[3] = { -e2[0], -e2[1], -e2[2] }, n3[3] = { -e3[0], -e3[1], -e3[2] };
      const float fAngle[3] = { EdgeAngle(e1, e2), EdgeAngle(n1, e3), EdgeAngle(n2, n3) };
      const int Tri[3] = { i0, i1, i2 };
      for (int c = 0; c < 3; c++)
      {
        float *a = &Acc[(size_t)Tri[c] * 9];
        for (int k = 0; k < 3; k++)
        {
          a[k] += T[k];
          a[3 + k] += B[k];
          a[6 + k] += N[k];
        }
        if (!bFace)
          continue;
        float *f = &Faces[(size_t)Tri[c] * 4];
        for (int k = 0; k < 3; k++)
          f[k] += U[k] * fAngle[c];
        f[3] += fAngle[c];
      }
    }

    for (int v = 0; v < nVerts; v++)
    {
      const float *a = &Acc[(size_t)v * 9];
      float N[3] = { a[6], a[7], a[8] };
      if (m.pNormals && v < m.nNormals)
      {
        memcpy(N, m.pNormals + (size_t)v * m.nNormalStride, 12);
        // The vertex's own normal, unless it does not fit its triangles.
        float F[3] = { Faces[(size_t)v * 4], Faces[(size_t)v * 4 + 1], Faces[(size_t)v * 4 + 2] };
        const float fAngles = Faces[(size_t)v * 4 + 3];
        float S[3] = { N[0], N[1], N[2] };
        if (fAngles > 0 && Normalize3(F) > FACES_AGREE * fAngles && Normalize3(S) > 1e-12f && Dot3(S, F) < NORMAL_FIT_COS)
          memcpy(N, F, 12);
      }
      if (Normalize3(N) <= 1e-12f)
      {
        N[0] = 0;
        N[1] = 0;
        N[2] = 1;
      }
      float T[3] = { a[0], a[1], a[2] };
      const float d = T[0] * N[0] + T[1] * N[1] + T[2] * N[2];
      for (int k = 0; k < 3; k++)
        T[k] -= N[k] * d;
      if (Normalize3(T) <= 1e-12f)
      {
        const float x[3] = { 1, 0, 0 }, y[3] = { 0, 1, 0 };
        Cross(fabsf(N[0]) < 0.9f ? x : y, N, T);
        Normalize3(T);
      }
      const float B[3] = { a[3], a[4], a[5] };
      float BN[3];
      Cross(N, T, BN);
      if (BN[0] * B[0] + BN[1] * B[1] + BN[2] * B[2] < 0)
        for (float &f : BN)
          f = -f;
      SFrame &o = pOut[v];
      memcpy(o.T, T, 12);
      memcpy(o.B, BN, 12);
      memcpy(o.N, N, 12);
    }
  }
}

#endif
