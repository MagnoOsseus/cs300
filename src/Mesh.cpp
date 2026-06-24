#include "Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <unordered_map>

// Hash to use glm::vec3 as a key.
struct Vec3Hash
{
    size_t operator()(const glm::vec3& v) const noexcept
    {
        auto h = [](float f) { return std::hash<float>{}(f); };
        size_t seed = h(v.x);
        seed ^= h(v.y) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        seed ^= h(v.z) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// Exact comparator for glm::vec3.
struct Vec3Equal
{
    bool operator()(const glm::vec3& a, const glm::vec3& b) const noexcept
    { return a.x == b.x && a.y == b.y && a.z == b.z; }
};

// Key to merge vertices by position + UV.
struct VertKey { glm::vec3 pos; glm::vec2 uv; };

// Hash for VertKey.
struct VertKeyHash
{
    size_t operator()(const VertKey& k) const noexcept
    {
        Vec3Hash h3;
        auto hf = [](float f){ return std::hash<float>{}(f); };
        size_t seed = h3(k.pos);
        seed ^= hf(k.uv.x) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        seed ^= hf(k.uv.y) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// Comparator for VertKey.
struct VertKeyEqual
{
    bool operator()(const VertKey& a, const VertKey& b) const noexcept
    {
        return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z &&
               a.uv.x  == b.uv.x  && a.uv.y  == b.uv.y;
    }
};

// Builds a non-indexed-like vertex buffer with one normal per triangle.
// This produces flat shading.
void Mesh::BuildFaceNormals(std::vector<Vertex>& verts,
                            std::vector<unsigned int>& idx) const
{
    verts.clear();
    idx.clear();
    // One independent vertex per triangle corner.
    verts.reserve(tris_.size() * 3);
    idx.reserve(tris_.size() * 3);

    for (const auto& tri : tris_)
    {
        // Face normal from cross product.
        glm::vec3 e1 = tri.v[1].pos - tri.v[0].pos;
        glm::vec3 e2 = tri.v[2].pos - tri.v[0].pos;
        glm::vec3 n  = glm::normalize(glm::cross(e1, e2));

        // Tangent and bitangent from UV differences (lecture derivation).
        glm::vec2 dUV1 = tri.v[1].uv - tri.v[0].uv;
        glm::vec2 dUV2 = tri.v[2].uv - tri.v[0].uv;
        float det = dUV1.x * dUV2.y - dUV1.y * dUV2.x;
        glm::vec3 T(0.0f), B(0.0f);
        if (std::abs(det) > 1e-8f)
        {
            float r = 1.0f / det;
            T = glm::normalize((e1 * dUV2.y - e2 * dUV1.y) * r);
            B = glm::normalize((e2 * dUV1.x - e1 * dUV2.x) * r);
        }

        // Start index for this triangle's vertices.
        auto base = static_cast<unsigned int>(verts.size());
        for (int i = 0; i < 3; ++i)
        {
            Vertex vert;
            vert.position  = tri.v[i].pos;
            // All three vertices use the same face normal.
            vert.normal    = n;
            vert.uv        = tri.v[i].uv;
            vert.tangent   = T;
            vert.bitangent = B;
            verts.push_back(vert);
            idx.push_back(base + i);
        }
    }
}
// Builds indexed vertices with averaged normals per position.
// This produces smooth shading where geometry is shared.
void Mesh::BuildAveragedNormals(std::vector<Vertex>& verts,
                                std::vector<unsigned int>& idx) const
{
    verts.clear();
    idx.clear();

    // Per-triangle geometry data.
    struct TriData { glm::vec3 normal; glm::vec3 tangent; glm::vec3 bitangent; };
    std::vector<TriData> triData(tris_.size());

    for (size_t fi = 0; fi < tris_.size(); ++fi)
    {
        glm::vec3 e1 = tris_[fi].v[1].pos - tris_[fi].v[0].pos;
        glm::vec3 e2 = tris_[fi].v[2].pos - tris_[fi].v[0].pos;
        triData[fi].normal = glm::normalize(glm::cross(e1, e2));

        // Tangent and bitangent from UV (lecture derivation, accumulate before normalize).
        glm::vec2 dUV1 = tris_[fi].v[1].uv - tris_[fi].v[0].uv;
        glm::vec2 dUV2 = tris_[fi].v[2].uv - tris_[fi].v[0].uv;
        float det = dUV1.x * dUV2.y - dUV1.y * dUV2.x;
        if (std::abs(det) > 1e-8f)
        {
            float r = 1.0f / det;
            triData[fi].tangent   = (e1 * dUV2.y - e2 * dUV1.y) * r;
            triData[fi].bitangent = (e2 * dUV1.x - e1 * dUV2.x) * r;
        }
    }

    // Accumulate normals, tangents and bitangents per position (unweighted sum).
    using VecList = std::vector<glm::vec3>;
    std::unordered_map<glm::vec3, VecList, Vec3Hash, Vec3Equal> posToNormals;
    std::unordered_map<glm::vec3, glm::vec3, Vec3Hash, Vec3Equal> posToTangent;
    std::unordered_map<glm::vec3, glm::vec3, Vec3Hash, Vec3Equal> posToBitangent;

    for (size_t fi = 0; fi < tris_.size(); ++fi)
    {
        const glm::vec3& fn = triData[fi].normal;
        for (int vi = 0; vi < 3; ++vi)
        {
            const glm::vec3& pos = tris_[fi].v[vi].pos;
            VecList& list        = posToNormals[pos];

            bool dup = false;
            for (const auto& existing : list)
            {
                if (glm::length(existing - fn) < 1e-5f) { dup = true; break; }
            }
            if (!dup) list.push_back(fn);

            // Accumulate tangent and bitangent (do not normalize before averaging).
            posToTangent[pos]    += triData[fi].tangent;
            posToBitangent[pos]  += triData[fi].bitangent;
        }
    }

    // Normalize averaged normals, tangents and bitangents per position.
    std::unordered_map<glm::vec3, glm::vec3, Vec3Hash, Vec3Equal> posToAvgN;
    std::unordered_map<glm::vec3, glm::vec3, Vec3Hash, Vec3Equal> posToAvgT;
    std::unordered_map<glm::vec3, glm::vec3, Vec3Hash, Vec3Equal> posToAvgB;

    for (const auto& [pos, normals] : posToNormals)
    {
        glm::vec3 sum(0.0f);
        for (const auto& n : normals) sum += n;
        posToAvgN[pos] = glm::normalize(sum);
    }
    for (const auto& [pos, t] : posToTangent)
    {
        float len = glm::length(t);
        posToAvgT[pos] = (len > 1e-8f) ? t / len : glm::vec3(1.0f, 0.0f, 0.0f);
    }
    for (const auto& [pos, b] : posToBitangent)
    {
        float len = glm::length(b);
        posToAvgB[pos] = (len > 1e-8f) ? b / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Build final index buffer using unique (position, uv) pairs.
    std::unordered_map<VertKey, unsigned int, VertKeyHash, VertKeyEqual> vertMap;
    verts.reserve(tris_.size() * 3);
    idx.reserve(tris_.size() * 3);

    for (const auto& tri : tris_)
    {
        for (int vi = 0; vi < 3; ++vi)
        {
            VertKey key{ tri.v[vi].pos, tri.v[vi].uv };
            auto it = vertMap.find(key);
            if (it == vertMap.end())
            {
                // New vertex, add it.
                auto newIdx = static_cast<unsigned int>(verts.size());
                vertMap[key] = newIdx;
                Vertex vert;
                vert.position  = tri.v[vi].pos;
                vert.normal    = posToAvgN.at(tri.v[vi].pos);
                vert.uv        = tri.v[vi].uv;
                vert.tangent   = posToAvgT.at(tri.v[vi].pos);
                vert.bitangent = posToAvgB.at(tri.v[vi].pos);
                verts.push_back(vert);
                idx.push_back(newIdx);
            }
            else
            {
                // Reuse previously created vertex index.
                idx.push_back(it->second);
            }
        }
    }
}

// Creates and uploads VAO/VBO/EBO with vertex layout.
void Mesh::UploadBuffers(const std::vector<Vertex>&       verts,
                         const std::vector<unsigned int>& idx)
{
    // Create buffers on first upload.
    if (vao_ == 0) glGenVertexArrays(1, &vao_);
    if (vbo_ == 0) glGenBuffers(1, &vbo_);
    if (ebo_ == 0) glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    // Upload packed vertex data.
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);

    // Upload index data.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(idx.size() * sizeof(unsigned int)),
                 idx.data(), GL_STATIC_DRAW);

    // Attribute 0: position.
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));

    // Attribute 1: normal.
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));

    // Attribute 2: UV.
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));

    // Attribute 3: tangent.
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, tangent)));

    // Attribute 4: bitangent.
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, bitangent)));

    // Unbind to avoid accidental changes.
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Store index count for draw call.
    drawCount_ = static_cast<int>(idx.size());
}

// Creates and uploads line vertices used to draw normals.
void Mesh::UploadNormalLines(const std::vector<Vertex>& verts, float length)
{
    // Two points per vertex: base and tip.
    std::vector<glm::vec3> lineVerts;
    lineVerts.reserve(verts.size() * 2);
    for (const auto& v : verts)
    {
        // Start point at vertex position.
        lineVerts.push_back(v.position);
        // End point in normal direction.
        lineVerts.push_back(v.position + v.normal * length);
    }

    // Lazily create line VAO/VBO.
    if (normVao_ == 0) glGenVertexArrays(1, &normVao_);
    if (normVbo_ == 0) glGenBuffers(1, &normVbo_);

    glBindVertexArray(normVao_);
    glBindBuffer(GL_ARRAY_BUFFER, normVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(lineVerts.size() * sizeof(glm::vec3)),
                 lineVerts.data(), GL_STATIC_DRAW);

    // Single vec3 attribute for line vertices.
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Store vertex count for GL_LINES draw.
    normCount_ = static_cast<int>(lineVerts.size());
}

// Uploads mesh data according to selected normal mode.
void Mesh::Upload(bool faceNormals)
{
    faceNormals_ = faceNormals;

    std::vector<Vertex>       verts;
    std::vector<unsigned int> idx;

    if (faceNormals_)
        BuildFaceNormals(verts, idx);
    else
        BuildAveragedNormals(verts, idx);

    UploadBuffers(verts, idx);
    UploadNormalLines(verts);
}

// Switches normal mode and rebuilds GPU buffers.
void Mesh::SetNormalMode(bool faceNormals)
{
    if (!tris_.empty())
        Upload(faceNormals);
}

// Draws indexed triangles.
void Mesh::Draw() const
{
    if (vao_ == 0 || drawCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, drawCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// Draws normal visualization lines.
void Mesh::DrawNormals() const
{
    if (normVao_ == 0 || normCount_ == 0) return;
    glBindVertexArray(normVao_);
    glDrawArrays(GL_LINES, 0, normCount_);
    glBindVertexArray(0);
}

// Releases all allocated GPU resources.
void Mesh::Free()
{
    if (ebo_)     { glDeleteBuffers(1, &ebo_);          ebo_     = 0; }
    if (vbo_)     { glDeleteBuffers(1, &vbo_);          vbo_     = 0; }
    if (vao_)     { glDeleteVertexArrays(1, &vao_);     vao_     = 0; }
    if (normVbo_) { glDeleteBuffers(1, &normVbo_);      normVbo_ = 0; }
    if (normVao_) { glDeleteVertexArrays(1, &normVao_); normVao_ = 0; }
    drawCount_ = normCount_ = 0;
}

// Generates a plane in the local [-0.5, 0.5] range.
Mesh Mesh::MakePlane()
{
    Mesh m;
    m.tris_.resize(2);

    // Triangle 0.
    m.tris_[0].v[0] = {glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f)};
    m.tris_[0].v[1] = {glm::vec3( 0.5f, -0.5f, 0.0f), glm::vec2(1.0f, 0.0f)};
    m.tris_[0].v[2] = {glm::vec3( 0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f)};

    // Triangle 1.
    m.tris_[1].v[0] = {glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f)};
    m.tris_[1].v[1] = {glm::vec3( 0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f)};
    m.tris_[1].v[2] = {glm::vec3(-0.5f,  0.5f, 0.0f), glm::vec2(0.0f, 1.0f)};

    return m;
}

// Generates a cube with per-face UV mapping.
Mesh Mesh::MakeCube()
{
    Mesh m;

    // Quad definition for each cube face.
    struct FaceDef { glm::vec3 verts[4]; };

    FaceDef faces[6] = {
        // +Z front face.
        {{{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}},
        // -Z back face.
        {{{ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}}},
        // +X right face.
        {{{ 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}}},
        // -X left face.
        {{{-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}}},
        // +Y top face.
        {{{-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}}},
        // -Y bottom face.
        {{{-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}}},
    };

    // Standard UV layout: (0,0)=bottom-left, (1,0)=bottom-right, (1,1)=top-right, (0,1)=top-left.
    glm::vec2 uvsStd[4] = {{0,0},{1,0},{1,1},{0,1}};

    // Top face (+Y) verts: v0(-x,+z) v1(+x,+z) v2(+x,-z) v3(-x,-z).
    // U = 1 at -x, 0 at +x; V = 1 at -z (back), 0 at +z (front).
    // → yellow(1,1) at back-left, black(0,0) at front-right.
    glm::vec2 uvsTop[4] = {{1,0},{0,0},{0,1},{1,1}};

    // Bottom face (-Y) verts: v0(-x,-z) v1(+x,-z) v2(+x,+z) v3(-x,+z).
    // Swap green<->red and black<->yellow on the bottom face.
    glm::vec2 uvsBtm[4] = {{1,0},{1,1},{0,1},{0,0}};

    for (int fi = 0; fi < 6; ++fi)
    {
        const glm::vec2* uvs = (fi == 4) ? uvsTop : (fi == 5) ? uvsBtm : uvsStd;
        const auto& f = faces[fi];

        RawTri t1, t2;
        t1.v[0] = {f.verts[0], uvs[0]};
        t1.v[1] = {f.verts[1], uvs[1]};
        t1.v[2] = {f.verts[2], uvs[2]};

        t2.v[0] = {f.verts[0], uvs[0]};
        t2.v[1] = {f.verts[2], uvs[2]};
        t2.v[2] = {f.verts[3], uvs[3]};

        m.tris_.push_back(t1);
        m.tris_.push_back(t2);
    }

    return m;
}

// Generates a cone with side triangles and a base cap.
Mesh Mesh::MakeCone(int slices)
{
    if (slices < 4) slices = 4;
    Mesh m;

    const float pi  = glm::pi<float>();
    const float r   = 0.5f;  
    const float top = 0.5f;  
    const float bot = -0.5f; 

    glm::vec3 apex(0.0f, top, 0.0f);
    glm::vec3 baseCenter(0.0f, bot, 0.0f);

    for (int i = 0; i < slices; ++i)
    {
        float a0 = 2.0f * pi * float(i)           / float(slices);
        // Use modulo so the last slice closes exactly on vertex 0.
        float aN = 2.0f * pi * float((i + 1) % slices) / float(slices);

        glm::vec3 b0(r * std::cos(a0), bot, r * std::sin(a0));
        glm::vec3 b1(r * std::cos(aN), bot, r * std::sin(aN));

        // Side triangle using cylindrical UVs.
        {
            // u follows angle around the cone, v goes bottom->top.
            float u0   = float(i)     / float(slices);
            float u1   = float(i + 1) / float(slices);
            float uMid = (u0 + u1) * 0.5f;

            RawTri t;
            t.v[0] = {apex, glm::vec2(uMid, 1.0f)};
            t.v[1] = {b1,   glm::vec2(u1,   0.0f)};
            t.v[2] = {b0,   glm::vec2(u0,   0.0f)};
            m.tris_.push_back(t);
        }

        // Base cap: angular UVs matching cone top style.
        {
            float u0   = float(i)     / float(slices);
            float u1   = float(i + 1) / float(slices);
            float uMid = (u0 + u1) * 0.5f;

            RawTri t;
            t.v[0] = {baseCenter, glm::vec2(uMid, 0.0f)};
            t.v[1] = {b0,         glm::vec2(u0,   0.0f)};
            t.v[2] = {b1,         glm::vec2(u1,   0.0f)};
            m.tris_.push_back(t);
        }
    }

    return m;
}

// Generates a cylinder with side band and top/bottom caps.
Mesh Mesh::MakeCylinder(int slices)
{
    if (slices < 4) slices = 4;
    Mesh m;

    const float pi  = glm::pi<float>();
    const float r   = 0.5f;
    const float top = 0.5f;
    const float bot = -0.5f;

    glm::vec3 topCenter(0.0f, top, 0.0f);
    glm::vec3 botCenter(0.0f, bot, 0.0f);

    for (int i = 0; i < slices; ++i)
    {
        float a0 = 2.0f * pi * float(i)               / float(slices);
        // Use modulo so the last slice closes exactly on vertex 0.
        float aN = 2.0f * pi * float((i + 1) % slices) / float(slices);

        glm::vec3 t0(r * std::cos(a0), top, r * std::sin(a0));
        glm::vec3 t1(r * std::cos(aN), top, r * std::sin(aN));
        glm::vec3 b0(r * std::cos(a0), bot, r * std::sin(a0));
        glm::vec3 b1(r * std::cos(aN), bot, r * std::sin(aN));

        // UV slice range for this side segment.
        float u0 = float(i)     / float(slices);
        float u1 = float(i + 1) / float(slices);

        // Side quad split into two triangles.
        {
            RawTri ta;
            ta.v[0] = {b0, glm::vec2(u0, 0.0f)};
            ta.v[1] = {t0, glm::vec2(u0, 1.0f)};
            ta.v[2] = {t1, glm::vec2(u1, 1.0f)};
            m.tris_.push_back(ta);

            RawTri tb;
            tb.v[0] = {b0, glm::vec2(u0, 0.0f)};
            tb.v[1] = {t1, glm::vec2(u1, 1.0f)};
            tb.v[2] = {b1, glm::vec2(u1, 0.0f)};
            m.tris_.push_back(tb);
        }

        // Top cap: cylindrical UVs — u=angle, v=1 (top edge of texture).
        {
            float uMid = (u0 + u1) * 0.5f;
            RawTri t;
            t.v[0] = {topCenter, glm::vec2(uMid, 1.0f)};
            t.v[1] = {t1,        glm::vec2(u1,   1.0f)};
            t.v[2] = {t0,        glm::vec2(u0,   1.0f)};
            m.tris_.push_back(t);
        }

        // Bottom cap: cylindrical UVs — u=angle, v=0 (bottom edge of texture).
        {
            float uMid = (u0 + u1) * 0.5f;
            RawTri t;
            t.v[0] = {botCenter, glm::vec2(uMid, 0.0f)};
            t.v[1] = {b0,        glm::vec2(u0,   0.0f)};
            t.v[2] = {b1,        glm::vec2(u1,   0.0f)};
            m.tris_.push_back(t);
        }
    }

    return m;
}

// Generates a sphere from rings and slices.
Mesh Mesh::MakeSphere(int slices, int rings)
{
    if (slices < 4) slices = 4;
    if (rings  < 2) rings  = 2;

    Mesh m;
    const float pi = glm::pi<float>();

    // Helpers to get sphere position and UV at a ring/slice.
    auto getPos = [&](int ring, int slice) -> glm::vec3 {
        if (ring == 0)     return glm::vec3(0.0f,  0.5f, 0.0f);
        if (ring == rings) return glm::vec3(0.0f, -0.5f, 0.0f);
        float theta = pi * float(ring)  / float(rings);
        float phi   = 2.0f * pi * float(slice % slices) / float(slices);
        return glm::vec3(
            0.5f * std::sin(theta) * std::cos(phi),
            0.5f * std::cos(theta),
            0.5f * std::sin(theta) * std::sin(phi));
    };
    auto getUV = [&](int ring, int slice) -> glm::vec2 {
        // U goes around (0 at seam, 1 at seam), V=1 at north pole, V=0 at south pole.
        return glm::vec2(float(slice) / float(slices),
                         1.0f - float(ring) / float(rings));
    };

    for (int i = 0; i < rings; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            // getPos wraps (slice % slices) so the seam closes exactly.
            glm::vec3 p00 = getPos(i,   j);
            glm::vec3 p10 = getPos(i,   j + 1);
            glm::vec3 p01 = getPos(i+1, j);
            glm::vec3 p11 = getPos(i+1, j + 1);

            glm::vec2 uv00 = getUV(i,   j);
            glm::vec2 uv10 = getUV(i,   j + 1);
            glm::vec2 uv01 = getUV(i+1, j);
            glm::vec2 uv11 = getUV(i+1, j + 1);

            if (i == 0)
            {
                // Top fan.
                RawTri t;
                t.v[0] = {p00, uv00};
                t.v[1] = {p11, uv11};
                t.v[2] = {p01, uv01};
                m.tris_.push_back(t);
            }
            else if (i == rings - 1)
            {
                // Bottom fan.
                RawTri t;
                t.v[0] = {p00, uv00};
                t.v[1] = {p10, uv10};
                t.v[2] = {p11, uv11};
                m.tris_.push_back(t);
            }
            else
            {
                // Middle band split into two triangles.
                RawTri t1, t2;
                t1.v[0] = {p00, uv00};
                t1.v[1] = {p10, uv10};
                t1.v[2] = {p11, uv11};

                t2.v[0] = {p00, uv00};
                t2.v[1] = {p11, uv11};
                t2.v[2] = {p01, uv01};

                m.tris_.push_back(t1);
                m.tris_.push_back(t2);
            }
        }
    }

    return m;
}

// Loads OBJ geometry and triangulates faces if needed.
Mesh Mesh::LoadOBJ(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Mesh::LoadOBJ: cannot open '" << path << "'\n";
        return MakeSphere(16, 8);   
    }

    // Source arrays from OBJ records.
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    // Face indices (OBJ is 1-based).
    struct FaceVert { int p, t, n; };
    struct Face { FaceVert v[3]; };
    std::vector<Face> faces;

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v")
        {
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
        }
        else if (token == "vn")
        {
            glm::vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        }
        else if (token == "vt")
        {
            glm::vec2 t;
            ss >> t.x >> t.y;
            texcoords.push_back(t);
        }
        else if (token == "f")
        {
            // Parse face and triangulate by fan.
            std::vector<FaceVert> fv;
            std::string tok;
            while (ss >> tok)
            {
                FaceVert fvert{0, 0, 0};
                // Supports formats: v, v/vt, v//vn, v/vt/vn.
                std::replace(tok.begin(), tok.end(), '/', ' ');
                std::istringstream ts(tok);
                ts >> fvert.p;
                if (!ts.eof()) ts >> fvert.t;
                if (!ts.eof()) ts >> fvert.n;
                fv.push_back(fvert);
            }
            
            // Fan triangulation.
            for (size_t k = 1; k + 1 < fv.size(); ++k)
            {
                Face face;
                face.v[0] = fv[0];
                face.v[1] = fv[k];
                face.v[2] = fv[k + 1];
                faces.push_back(face);
            }
        }
    }

    // Build internal raw triangle list.
    Mesh m;
    m.tris_.reserve(faces.size());

    for (const auto& face : faces)
    {
        RawTri tri;
        for (int i = 0; i < 3; ++i)
        {
            int pi = face.v[i].p - 1;
            int ti = face.v[i].t - 1;

            glm::vec3 pos(0.0f);
            glm::vec2 uv(0.0f);

            // Read available position and UV safely.
            if (pi >= 0 && pi < static_cast<int>(positions.size()))
                pos = positions[pi];
            if (ti >= 0 && ti < static_cast<int>(texcoords.size()))
                uv = texcoords[ti];

            tri.v[i] = {pos, uv};
        }
        m.tris_.push_back(tri);
    }

    if (m.tris_.empty())
    {
        std::cerr << "Mesh::LoadOBJ: no triangles in '" << path << "', using fallback.\n";
        return MakeSphere(16, 8);
    }

    return m;
}

// Exports mesh geometry to an OBJ file.
void Mesh::SaveOBJ(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open())
    {
        std::cerr << "Mesh::SaveOBJ: cannot write '" << path << "'\n";
        return;
    }

    // Build flat-shaded data for export.
    std::vector<Vertex>       verts;
    std::vector<unsigned int> idx;
    BuildFaceNormals(verts, idx);

    // Write vertex streams.
    f << "# Generated by CS300\n";
    for (const auto& v : verts)
        f << "v "  << v.position.x << ' ' << v.position.y << ' ' << v.position.z << '\n';
    for (const auto& v : verts)
        f << "vt " << v.uv.x       << ' ' << v.uv.y       << '\n';
    for (const auto& v : verts)
        f << "vn " << v.normal.x   << ' ' << v.normal.y   << ' ' << v.normal.z   << '\n';

    // Write faces using matching v/vt/vn indices.
    for (size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        auto a = idx[i] + 1, b = idx[i+1] + 1, c = idx[i+2] + 1;
        f << "f " << a << '/' << a << '/' << a
          << ' '  << b << '/' << b << '/' << b
          << ' '  << c << '/' << c << '/' << c << '\n';
    }
}
