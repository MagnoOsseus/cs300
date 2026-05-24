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

// ============================================================
//  Internal helpers
// ============================================================

// Hash / equality for glm::vec3 using exact bit comparison
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
struct Vec3Equal
{
    bool operator()(const glm::vec3& a, const glm::vec3& b) const noexcept
    { return a.x == b.x && a.y == b.y && a.z == b.z; }
};

// Hash / equality for (pos, uv) — used to de-duplicate indexed vertices
struct VertKey { glm::vec3 pos; glm::vec2 uv; };
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
struct VertKeyEqual
{
    bool operator()(const VertKey& a, const VertKey& b) const noexcept
    {
        return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z &&
               a.uv.x  == b.uv.x  && a.uv.y  == b.uv.y;
    }
};

// ============================================================
//  Normal builders
// ============================================================

void Mesh::BuildFaceNormals(std::vector<Vertex>& verts,
                            std::vector<unsigned int>& idx) const
{
    verts.clear();
    idx.clear();
    verts.reserve(tris_.size() * 3);
    idx.reserve(tris_.size() * 3);

    for (const auto& tri : tris_)
    {
        glm::vec3 e1 = tri.v[1].pos - tri.v[0].pos;
        glm::vec3 e2 = tri.v[2].pos - tri.v[0].pos;
        glm::vec3 n  = glm::normalize(glm::cross(e1, e2));

        auto base = static_cast<unsigned int>(verts.size());
        for (int i = 0; i < 3; ++i)
        {
            Vertex vert;
            vert.position = tri.v[i].pos;
            vert.normal   = n;
            vert.uv       = tri.v[i].uv;
            verts.push_back(vert);
            idx.push_back(base + i);
        }
    }
}

void Mesh::BuildAveragedNormals(std::vector<Vertex>& verts,
                                std::vector<unsigned int>& idx) const
{
    verts.clear();
    idx.clear();

    // ---- Step 1: compute a face normal for each triangle ----
    std::vector<glm::vec3> faceN(tris_.size());
    for (size_t fi = 0; fi < tris_.size(); ++fi)
    {
        glm::vec3 e1 = tris_[fi].v[1].pos - tris_[fi].v[0].pos;
        glm::vec3 e2 = tris_[fi].v[2].pos - tris_[fi].v[0].pos;
        faceN[fi]    = glm::normalize(glm::cross(e1, e2));
    }

    // ---- Step 2: for each unique position collect unique face normals ----
    // "If two faces share the same normal, count it only once."
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
                if (glm::length(existing - fn) < 1e-5f) { dup = true; break; }
            }
            if (!dup) list.push_back(fn);
        }
    }

    // ---- Step 3: average the collected normals per position ----
    std::unordered_map<glm::vec3, glm::vec3, Vec3Hash, Vec3Equal> posToAvgN;
    for (const auto& [pos, normals] : posToNormals)
    {
        glm::vec3 sum(0.0f);
        for (const auto& n : normals) sum += n;
        posToAvgN[pos] = glm::normalize(sum);
    }

    // ---- Step 4: build indexed vertex list (unique by pos+uv) ----
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
                auto newIdx = static_cast<unsigned int>(verts.size());
                vertMap[key] = newIdx;
                Vertex vert;
                vert.position = tri.v[vi].pos;
                vert.normal   = posToAvgN.at(tri.v[vi].pos);
                vert.uv       = tri.v[vi].uv;
                verts.push_back(vert);
                idx.push_back(newIdx);
            }
            else
            {
                idx.push_back(it->second);
            }
        }
    }
}

// ============================================================
//  GPU buffer management
// ============================================================

void Mesh::UploadBuffers(const std::vector<Vertex>&       verts,
                         const std::vector<unsigned int>& idx)
{
    if (vao_ == 0) glGenVertexArrays(1, &vao_);
    if (vbo_ == 0) glGenBuffers(1, &vbo_);
    if (ebo_ == 0) glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(idx.size() * sizeof(unsigned int)),
                 idx.data(), GL_STATIC_DRAW);

    // layout(location=0) position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    // layout(location=1) normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    // layout(location=2) uv
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    drawCount_ = static_cast<int>(idx.size());
}

void Mesh::UploadNormalLines(const std::vector<Vertex>& verts, float length)
{
    // Each vertex contributes 2 line points: start and end
    std::vector<glm::vec3> lineVerts;
    lineVerts.reserve(verts.size() * 2);
    for (const auto& v : verts)
    {
        lineVerts.push_back(v.position);
        lineVerts.push_back(v.position + v.normal * length);
    }

    if (normVao_ == 0) glGenVertexArrays(1, &normVao_);
    if (normVbo_ == 0) glGenBuffers(1, &normVbo_);

    glBindVertexArray(normVao_);
    glBindBuffer(GL_ARRAY_BUFFER, normVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(lineVerts.size() * sizeof(glm::vec3)),
                 lineVerts.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    normCount_ = static_cast<int>(lineVerts.size());
}

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

void Mesh::SetNormalMode(bool faceNormals)
{
    if (!tris_.empty())
        Upload(faceNormals);
}

void Mesh::Draw() const
{
    if (vao_ == 0 || drawCount_ == 0) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, drawCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::DrawNormals() const
{
    if (normVao_ == 0 || normCount_ == 0) return;
    glBindVertexArray(normVao_);
    glDrawArrays(GL_LINES, 0, normCount_);
    glBindVertexArray(0);
}

void Mesh::Free()
{
    if (ebo_)     { glDeleteBuffers(1, &ebo_);          ebo_     = 0; }
    if (vbo_)     { glDeleteBuffers(1, &vbo_);          vbo_     = 0; }
    if (vao_)     { glDeleteVertexArrays(1, &vao_);     vao_     = 0; }
    if (normVbo_) { glDeleteBuffers(1, &normVbo_);      normVbo_ = 0; }
    if (normVao_) { glDeleteVertexArrays(1, &normVao_); normVao_ = 0; }
    drawCount_ = normCount_ = 0;
}

// ============================================================
//  Shape factories
// ============================================================

Mesh Mesh::MakePlane()
{
    Mesh m;
    m.tris_.resize(2);

    // Triangle 0: BL, BR, TR
    m.tris_[0].v[0] = {glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f)};
    m.tris_[0].v[1] = {glm::vec3( 0.5f, -0.5f, 0.0f), glm::vec2(1.0f, 0.0f)};
    m.tris_[0].v[2] = {glm::vec3( 0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f)};

    // Triangle 1: BL, TR, TL
    m.tris_[1].v[0] = {glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec2(0.0f, 0.0f)};
    m.tris_[1].v[1] = {glm::vec3( 0.5f,  0.5f, 0.0f), glm::vec2(1.0f, 1.0f)};
    m.tris_[1].v[2] = {glm::vec3(-0.5f,  0.5f, 0.0f), glm::vec2(0.0f, 1.0f)};

    return m;
}

Mesh Mesh::MakeCube()
{
    Mesh m;

    // 6 faces defined as quads (CCW outward winding)
    struct FaceDef { glm::vec3 verts[4]; };

    FaceDef faces[6] = {
        // Front (+Z)
        {{{-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}},
        // Back  (-Z)
        {{{ 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}}},
        // Right (+X)
        {{{ 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f}}},
        // Left  (-X)
        {{{-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f}}},
        // Top   (+Y)
        {{{-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}}},
        // Bottom(-Y)
        {{{-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f}}},
    };

    // Per-face UV (each face spans [0,1]^2)
    glm::vec2 uvs[4] = {{0,0},{1,0},{1,1},{0,1}};

    for (const auto& f : faces)
    {
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

Mesh Mesh::MakeCone(int slices)
{
    if (slices < 4) slices = 4;
    Mesh m;

    const float pi  = glm::pi<float>();
    const float r   = 0.5f;  // base radius
    const float top = 0.5f;  // apex y
    const float bot = -0.5f; // base y

    glm::vec3 apex(0.0f, top, 0.0f);
    glm::vec3 baseCenter(0.0f, bot, 0.0f);

    for (int i = 0; i < slices; ++i)
    {
        float a0 = 2.0f * pi * float(i)     / float(slices);
        float a1 = 2.0f * pi * float(i + 1) / float(slices);

        glm::vec3 b0(r * std::cos(a0), bot, r * std::sin(a0));
        glm::vec3 b1(r * std::cos(a1), bot, r * std::sin(a1));

        // --- Lateral triangle: apex, b1, b0  (outward normal) ---
        {
            // Cylindrical UV: u = azimuth/2pi, v = 0 at base, 1 at apex
            float u0   = float(i)     / float(slices);
            float u1   = float(i + 1) / float(slices);
            float uMid = (u0 + u1) * 0.5f;

            RawTri t;
            t.v[0] = {apex, glm::vec2(uMid, 1.0f)};
            t.v[1] = {b1,   glm::vec2(u1,   0.0f)};
            t.v[2] = {b0,   glm::vec2(u0,   0.0f)};
            m.tris_.push_back(t);
        }

        // --- Base triangle: center, b0, b1  (normal −Y) ---
        {
            // Planar UV on base disc
            glm::vec2 uvCenter(0.5f, 0.5f);
            glm::vec2 uv0(0.5f + b0.x, 0.5f + b0.z);
            glm::vec2 uv1(0.5f + b1.x, 0.5f + b1.z);

            RawTri t;
            t.v[0] = {baseCenter, uvCenter};
            t.v[1] = {b0,         uv0};
            t.v[2] = {b1,         uv1};
            m.tris_.push_back(t);
        }
    }

    return m;
}

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
        float a0 = 2.0f * pi * float(i)     / float(slices);
        float a1 = 2.0f * pi * float(i + 1) / float(slices);

        glm::vec3 t0(r * std::cos(a0), top, r * std::sin(a0));
        glm::vec3 t1(r * std::cos(a1), top, r * std::sin(a1));
        glm::vec3 b0(r * std::cos(a0), bot, r * std::sin(a0));
        glm::vec3 b1(r * std::cos(a1), bot, r * std::sin(a1));

        float u0 = float(i)     / float(slices);
        float u1 = float(i + 1) / float(slices);

        // --- Lateral (2 triangles forming a quad) ---
        // Outward winding checked:  (b0, t0, t1) and (b0, t1, b1)
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

        // --- Top cap  (center, t1, t0)  → normal +Y ---
        {
            glm::vec2 uvC(0.5f, 0.5f);
            glm::vec2 uv1(0.5f + t1.x, 0.5f + t1.z);
            glm::vec2 uv0(0.5f + t0.x, 0.5f + t0.z);

            RawTri t;
            t.v[0] = {topCenter, uvC};
            t.v[1] = {t1,        uv1};
            t.v[2] = {t0,        uv0};
            m.tris_.push_back(t);
        }

        // --- Bottom cap  (center, b0, b1)  → normal −Y ---
        {
            glm::vec2 uvC(0.5f, 0.5f);
            glm::vec2 uv0(0.5f + b0.x, 0.5f + b0.z);
            glm::vec2 uv1(0.5f + b1.x, 0.5f + b1.z);

            RawTri t;
            t.v[0] = {botCenter, uvC};
            t.v[1] = {b0,        uv0};
            t.v[2] = {b1,        uv1};
            m.tris_.push_back(t);
        }
    }

    return m;
}

Mesh Mesh::MakeSphere(int slices, int rings)
{
    if (slices < 4) slices = 4;
    if (rings  < 2) rings  = 2;

    Mesh m;
    const float pi = glm::pi<float>();

    // Vertex position on unit sphere (radius 0.5)
    auto getPos = [&](int ring, int slice) -> glm::vec3 {
        float theta = pi * float(ring)  / float(rings);
        float phi   = 2.0f * pi * float(slice) / float(slices);
        return glm::vec3(
            0.5f * std::sin(theta) * std::cos(phi),
            0.5f * std::cos(theta),
            0.5f * std::sin(theta) * std::sin(phi));
    };
    auto getUV = [&](int ring, int slice) -> glm::vec2 {
        return glm::vec2(float(slice) / float(slices),
                         float(ring)  / float(rings));
    };

    for (int i = 0; i < rings; ++i)
    {
        for (int j = 0; j < slices; ++j)
        {
            int jNext = (j + 1) % (slices + 1); // slices+1 so edge UV is correct
            // We wrap by just using j+1 (it'll coincide positionally with j=0)

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
                // Top cap: single triangle (pole, p11, p01)
                RawTri t;
                t.v[0] = {p00, uv00};
                t.v[1] = {p11, uv11};
                t.v[2] = {p01, uv01};
                m.tris_.push_back(t);
            }
            else if (i == rings - 1)
            {
                // Bottom cap: single triangle (p00, p10, south-pole)
                RawTri t;
                t.v[0] = {p00, uv00};
                t.v[1] = {p10, uv10};
                t.v[2] = {p11, uv11};
                m.tris_.push_back(t);
            }
            else
            {
                // Middle band: quad → 2 triangles
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

// ============================================================
//  OBJ loader
// ============================================================

Mesh Mesh::LoadOBJ(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Mesh::LoadOBJ: cannot open '" << path << "'\n";
        return MakeSphere(16, 8);   // fallback
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;

    // Each face vertex is (pos_idx, uv_idx, norm_idx)  (1-based OBJ indices)
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
            // Triangulate simple faces (3 or 4 vertices)
            std::vector<FaceVert> fv;
            std::string tok;
            while (ss >> tok)
            {
                FaceVert fvert{0, 0, 0};
                // Format: v   or v/vt   or v/vt/vn   or v//vn
                std::replace(tok.begin(), tok.end(), '/', ' ');
                std::istringstream ts(tok);
                ts >> fvert.p;
                if (!ts.eof()) ts >> fvert.t;
                if (!ts.eof()) ts >> fvert.n;
                fv.push_back(fvert);
            }
            // Fan triangulation
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

    // Build raw triangles from parsed data
    Mesh m;
    m.tris_.reserve(faces.size());

    for (const auto& face : faces)
    {
        RawTri tri;
        for (int i = 0; i < 3; ++i)
        {
            int pi = face.v[i].p - 1; // OBJ is 1-based
            int ti = face.v[i].t - 1;

            glm::vec3 pos(0.0f);
            glm::vec2 uv(0.0f);

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

// ============================================================
//  OBJ export  (helper for pre-generating data files)
// ============================================================

void Mesh::SaveOBJ(const std::string& path) const
{
    std::ofstream f(path);
    if (!f.is_open())
    {
        std::cerr << "Mesh::SaveOBJ: cannot write '" << path << "'\n";
        return;
    }

    // Build a face-normal version to export
    std::vector<Vertex>       verts;
    std::vector<unsigned int> idx;
    BuildFaceNormals(verts, idx);

    f << "# Generated by CS300\n";
    for (const auto& v : verts)
        f << "v "  << v.position.x << ' ' << v.position.y << ' ' << v.position.z << '\n';
    for (const auto& v : verts)
        f << "vt " << v.uv.x       << ' ' << v.uv.y       << '\n';
    for (const auto& v : verts)
        f << "vn " << v.normal.x   << ' ' << v.normal.y   << ' ' << v.normal.z   << '\n';

    for (size_t i = 0; i + 2 < idx.size(); i += 3)
    {
        auto a = idx[i] + 1, b = idx[i+1] + 1, c = idx[i+2] + 1;
        f << "f " << a << '/' << a << '/' << a
          << ' '  << b << '/' << b << '/' << b
          << ' '  << c << '/' << c << '/' << c << '\n';
    }
}
