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

// Hash for glm::vec3 keys.
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

// Exact compare for glm::vec3.
struct Vec3Equal
{
    bool operator()(const glm::vec3& a, const glm::vec3& b) const noexcept
    { return a.x == b.x && a.y == b.y && a.z == b.z; }
};

// Key for position + UV merge.
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

// Compare VertKey values.
struct VertKeyEqual
{
    bool operator()(const VertKey& a, const VertKey& b) const noexcept
    {
        return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z &&
               a.uv.x  == b.uv.x  && a.uv.y  == b.uv.y;
    }
};

static glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback)
{
    const float len = glm::length(v);
    if (len < 1e-6f)
    {
        return fallback;
    }
    return v / len;
}

static void ComputeTriangleTangentBitangent(const glm::vec3& p0,
                                            const glm::vec3& p1,
                                            const glm::vec3& p2,
                                            const glm::vec2& uv0,
                                            const glm::vec2& uv1,
                                            const glm::vec2& uv2,
                                            glm::vec3& tangent,
                                            glm::vec3& bitangent)
{
    const glm::vec3 v1 = p1 - p0;
    const glm::vec3 v2 = p2 - p0;
    const glm::vec2 dUV1 = uv1 - uv0;
    const glm::vec2 dUV2 = uv2 - uv0;

    const float det = dUV1.x * dUV2.y - dUV1.y * dUV2.x;
    if (std::abs(det) > 1e-8f)
    {
        const float r = 1.0f / det;
        tangent = (v1 * dUV2.y - v2 * dUV1.y) * r;
        bitangent = (v2 * dUV1.x - v1 * dUV2.x) * r;
        return;
    }

    const glm::vec3 n = SafeNormalize(glm::cross(v1, v2), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 up = (std::abs(n.y) > 0.999f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    tangent = SafeNormalize(glm::cross(up, n), glm::vec3(1.0f, 0.0f, 0.0f));
    bitangent = SafeNormalize(glm::cross(n, tangent), glm::vec3(0.0f, 0.0f, 1.0f));
}

// Build flat-shaded vertices.
void Mesh::BuildFaceNormals(std::vector<Vertex>& verts,
                            std::vector<unsigned int>& idx) const
{
    verts.clear();
    idx.clear();
    // One vertex per triangle corner.
    verts.reserve(tris_.size() * 3);
    idx.reserve(tris_.size() * 3);

    for (const auto& tri : tris_)
    {
        // Compute face normal.
        glm::vec3 e1 = tri.v[1].pos - tri.v[0].pos;
        glm::vec3 e2 = tri.v[2].pos - tri.v[0].pos;
        glm::vec3 n  = glm::normalize(glm::cross(e1, e2));
        glm::vec3 t(0.0f), b(0.0f);
        ComputeTriangleTangentBitangent(tri.v[0].pos, tri.v[1].pos, tri.v[2].pos,
                                        tri.v[0].uv, tri.v[1].uv, tri.v[2].uv, t, b);

        // Base index for this triangle.
        auto base = static_cast<unsigned int>(verts.size());
        for (int i = 0; i < 3; ++i)
        {
            Vertex vert;
            vert.position = tri.v[i].pos;
            // Use the same normal for the 3 corners.
            vert.normal   = n;
            vert.uv       = tri.v[i].uv;
            vert.tangent = t;
            vert.bitangent = b;
            verts.push_back(vert);
            idx.push_back(base + i);
        }
    }
}
// Build smooth-shaded indexed vertices.
void Mesh::BuildAveragedNormals(std::vector<Vertex>& verts,
                                std::vector<unsigned int>& idx) const
{
    verts.clear();
    idx.clear();

    // Compute one normal per triangle.
    std::vector<glm::vec3> faceN(tris_.size());
    for (size_t fi = 0; fi < tris_.size(); ++fi)
    {
        glm::vec3 e1 = tris_[fi].v[1].pos - tris_[fi].v[0].pos;
        glm::vec3 e2 = tris_[fi].v[2].pos - tris_[fi].v[0].pos;
        faceN[fi]    = glm::normalize(glm::cross(e1, e2));
    }

    // Group face normals by vertex position.
    using NormalList = std::vector<glm::vec3>;
    std::unordered_map<glm::vec3, NormalList, Vec3Hash, Vec3Equal> posToNormals;

    for (size_t fi = 0; fi < tris_.size(); ++fi)
    {
        for (int vi = 0; vi < 3; ++vi)
        {
            const glm::vec3& pos = tris_[fi].v[vi].pos;
            NormalList& list     = posToNormals[pos];
            const glm::vec3& fn  = faceN[fi];

            bool dup = false;
            for (const auto& existing : list)
            {
                // Skip almost duplicate normals.
                if (glm::length(existing - fn) < 1e-5f) { dup = true; break; }
            }
            if (!dup) list.push_back(fn);
        }
    }

    // Average normals per position.
    std::unordered_map<glm::vec3, glm::vec3, Vec3Hash, Vec3Equal> posToAvgN;
    for (const auto& [pos, normals] : posToNormals)
    {
        glm::vec3 sum(0.0f);
        for (const auto& n : normals) sum += n;
        posToAvgN[pos] = glm::normalize(sum);
    }

    // Build index buffer from unique (pos, uv).
    std::unordered_map<VertKey, unsigned int, VertKeyHash, VertKeyEqual> vertMap;
    verts.reserve(tris_.size() * 3);
    idx.reserve(tris_.size() * 3);
    std::vector<glm::vec3> tangentSums;
    std::vector<glm::vec3> bitangentSums;

    for (const auto& tri : tris_)
    {
        glm::vec3 triTangent(0.0f), triBitangent(0.0f);
        ComputeTriangleTangentBitangent(tri.v[0].pos, tri.v[1].pos, tri.v[2].pos,
                                        tri.v[0].uv, tri.v[1].uv, tri.v[2].uv,
                                        triTangent, triBitangent);

        for (int vi = 0; vi < 3; ++vi)
        {
            VertKey key{ tri.v[vi].pos, tri.v[vi].uv };
            auto it = vertMap.find(key);
            if (it == vertMap.end())
            {
                // Add new vertex.
                auto newIdx = static_cast<unsigned int>(verts.size());
                vertMap[key] = newIdx;
                Vertex vert;
                vert.position = tri.v[vi].pos;
                vert.normal   = posToAvgN.at(tri.v[vi].pos);
                vert.uv       = tri.v[vi].uv;
                vert.tangent = glm::vec3(0.0f);
                vert.bitangent = glm::vec3(0.0f);
                verts.push_back(vert);
                tangentSums.push_back(glm::vec3(0.0f));
                bitangentSums.push_back(glm::vec3(0.0f));
                idx.push_back(newIdx);
                tangentSums[newIdx] += triTangent;
                bitangentSums[newIdx] += triBitangent;
            }
            else
            {
                // Reuse existing vertex.
                idx.push_back(it->second);
                tangentSums[it->second] += triTangent;
                bitangentSums[it->second] += triBitangent;
            }
        }
    }

    for (size_t i = 0; i < verts.size(); ++i)
    {
        const glm::vec3 n = SafeNormalize(verts[i].normal, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 t = tangentSums[i] - n * glm::dot(n, tangentSums[i]);
        t = SafeNormalize(t, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::vec3 b = bitangentSums[i] - n * glm::dot(n, bitangentSums[i]);
        b -= t * glm::dot(t, b);
        b = SafeNormalize(b, glm::cross(n, t));
        verts[i].tangent = t;
        verts[i].bitangent = b;
    }
}

// Create and upload VAO/VBO/EBO.
void Mesh::UploadBuffers(const std::vector<Vertex>&       verts,
                         const std::vector<unsigned int>& idx)
{
    // Create buffers on first upload.
    if (vao_ == 0) glGenVertexArrays(1, &vao_);
    if (vbo_ == 0) glGenBuffers(1, &vbo_);
    if (ebo_ == 0) glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    // Upload vertex data.
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

    // Unbind buffers.
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Store index count.
    drawCount_ = static_cast<int>(idx.size());
}

// Create and upload normal lines.
void Mesh::UploadNormalLines(const std::vector<Vertex>& verts, float length)
{
    // Two points per normal line.
    std::vector<glm::vec3> lineVerts;
    lineVerts.reserve(verts.size() * 2);
    for (const auto& v : verts)
    {
        // Line start.
        lineVerts.push_back(v.position);
        // Line end.
        lineVerts.push_back(v.position + v.normal * length);
    }

    // Create line VAO/VBO on demand.
    if (normVao_ == 0) glGenVertexArrays(1, &normVao_);
    if (normVbo_ == 0) glGenBuffers(1, &normVbo_);

    glBindVertexArray(normVao_);
    glBindBuffer(GL_ARRAY_BUFFER, normVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(lineVerts.size() * sizeof(glm::vec3)),
                 lineVerts.data(), GL_STATIC_DRAW);

    // Single position attribute.
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Store line vertex count.
    normCount_ = static_cast<int>(lineVerts.size());
}

// Upload mesh using selected normal mode.
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

// Change normal mode and rebuild buffers.
void Mesh::SetNormalMode(bool faceNormals)
{
    if (!tris_.empty())
        Upload(faceNormals);
}

// Draw triangles.
void Mesh::Draw() const
{
    if (vao_ == 0 || drawCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, drawCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// Draw normal debug lines.
void Mesh::DrawNormals() const
{
    if (normVao_ == 0 || normCount_ == 0) return;
    glBindVertexArray(normVao_);
    glDrawArrays(GL_LINES, 0, normCount_);
    glBindVertexArray(0);
}

// Release GPU resources.
void Mesh::Free()
{
    if (ebo_)     { glDeleteBuffers(1, &ebo_);          ebo_     = 0; }
    if (vbo_)     { glDeleteBuffers(1, &vbo_);          vbo_     = 0; }
    if (vao_)     { glDeleteVertexArrays(1, &vao_);     vao_     = 0; }
    if (normVbo_) { glDeleteBuffers(1, &normVbo_);      normVbo_ = 0; }
    if (normVao_) { glDeleteVertexArrays(1, &normVao_); normVao_ = 0; }
    drawCount_ = normCount_ = 0;
}

// Build a plane in [-0.5, 0.5].
Mesh Mesh::MakePlane()
{
    Mesh m;
    m.tris_.resize(2);

    // First triangle.
    m.tris_[0].v[0] = {glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f)};
    m.tris_[0].v[1] = {glm::vec3( 0.5f, -0.5f, 0.0f), glm::vec2(1.0f, 0.0f)};
    m.tris_[0].v[2] = {glm::vec3( 0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f)};

    // Second triangle.
    m.tris_[1].v[0] = {glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f)};
    m.tris_[1].v[1] = {glm::vec3( 0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f)};
    m.tris_[1].v[2] = {glm::vec3(-0.5f,  0.5f, 0.0f), glm::vec2(0.0f, 1.0f)};

    return m;
}

// Build a cube with face UVs.
Mesh Mesh::MakeCube()
{
    Mesh m;

    // 4 corners per face.
    struct FaceDef { glm::vec3 verts[4]; };

    FaceDef faces[6] = {
        // +Z face.
        {{{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}},
        // -Z face.
        {{{ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}}},
        // +X face.
        {{{ 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}}},
        // -X face.
        {{{-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}}},
        // +Y face.
        {{{-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}}},
        // -Y face.
        {{{-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}}},
    };

    // Default UV order.
    glm::vec2 uvsStd[4] = {{0,0},{1,0},{1,1},{0,1}};

    // Custom UVs for top face.
    glm::vec2 uvsTop[4] = {{1,0},{0,0},{0,1},{1,1}};

    // Custom UVs for bottom face.
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

// Build a cone with side and base.
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
        // Wrap to close the seam.
        float aN = 2.0f * pi * float((i + 1) % slices) / float(slices);

        glm::vec3 b0(r * std::cos(a0), bot, r * std::sin(a0));
        glm::vec3 b1(r * std::cos(aN), bot, r * std::sin(aN));

        // Side triangle.
        {
            // UV follows angle and height.
            float u0   = float(i)     / float(slices);
            float u1   = float(i + 1) / float(slices);
            float uMid = (u0 + u1) * 0.5f;

            RawTri t;
            t.v[0] = {apex, glm::vec2(uMid, 1.0f)};
            t.v[1] = {b1,   glm::vec2(u1,   0.0f)};
            t.v[2] = {b0,   glm::vec2(u0,   0.0f)};
            m.tris_.push_back(t);
        }

        // Base cap triangle.
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

// Build a cylinder with caps.
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
        // Wrap to close the seam.
        float aN = 2.0f * pi * float((i + 1) % slices) / float(slices);

        glm::vec3 t0(r * std::cos(a0), top, r * std::sin(a0));
        glm::vec3 t1(r * std::cos(aN), top, r * std::sin(aN));
        glm::vec3 b0(r * std::cos(a0), bot, r * std::sin(a0));
        glm::vec3 b1(r * std::cos(aN), bot, r * std::sin(aN));

        // UV range for this slice.
        float u0 = float(i)     / float(slices);
        float u1 = float(i + 1) / float(slices);

        // Side quad as two triangles.
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

        // Top cap triangle.
        {
            float uMid = (u0 + u1) * 0.5f;
            RawTri t;
            t.v[0] = {topCenter, glm::vec2(uMid, 1.0f)};
            t.v[1] = {t1,        glm::vec2(u1,   1.0f)};
            t.v[2] = {t0,        glm::vec2(u0,   1.0f)};
            m.tris_.push_back(t);
        }

        // Bottom cap triangle.
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

// Build a sphere from rings and slices.
Mesh Mesh::MakeSphere(int slices, int rings)
{
    if (slices < 4) slices = 4;
    if (rings  < 2) rings  = 2;

    Mesh m;
    const float pi = glm::pi<float>();

    // Helpers for sphere position and UV.
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
        // UV wraps around ring and pole.
        return glm::vec2(float(slice) / float(slices),
                         1.0f - float(ring) / float(rings));
    };

    for (int i = 0; i < rings; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            // getPos wraps slices to close seam.
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
                // Top fan triangle.
                RawTri t;
                t.v[0] = {p00, uv00};
                t.v[1] = {p11, uv11};
                t.v[2] = {p01, uv01};
                m.tris_.push_back(t);
            }
            else if (i == rings - 1)
            {
                // Bottom fan triangle.
                RawTri t;
                t.v[0] = {p00, uv00};
                t.v[1] = {p10, uv10};
                t.v[2] = {p11, uv11};
                m.tris_.push_back(t);
            }
            else
            {
                // Middle band triangles.
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

// Load OBJ and triangulate faces.
Mesh Mesh::LoadOBJ(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Mesh::LoadOBJ: cannot open '" << path << "'\n";
        return MakeSphere(16, 8);   
    }

    // OBJ source arrays.
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    // OBJ face indices are 1-based.
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
            // Parse one face.
            std::vector<FaceVert> fv;
            std::string tok;
            while (ss >> tok)
            {
                FaceVert fvert{0, 0, 0};
                // Supports v, v/vt, v//vn, v/vt/vn.
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

    // Build internal triangle list.
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

            // Read position and UV safely.
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

// Export mesh as OBJ file.
void Mesh::SaveOBJ(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open())
    {
        std::cerr << "Mesh::SaveOBJ: cannot write '" << path << "'\n";
        return;
    }

    // Build flat data for export.
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

    // Write faces with v/vt/vn indices.
    for (size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        auto a = idx[i] + 1, b = idx[i+1] + 1, c = idx[i+2] + 1;
        f << "f " << a << '/' << a << '/' << a
          << ' '  << b << '/' << b << '/' << b
          << ' '  << c << '/' << c << '/' << c << '\n';
    }
}
