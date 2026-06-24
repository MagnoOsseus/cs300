#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Vertex data sent to GPU.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;
    glm::vec3 bitangent;
};

// Mesh data and OpenGL buffers.
class Mesh
{
public:
    Mesh()  = default;
    ~Mesh() = default;

    // Disable copy, allow move.
    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&)                 = default;
    Mesh& operator=(Mesh&&)      = default;

    // Upload mesh buffers.
    // true = face normals, false = smooth normals.
    void Upload(bool faceNormals);

    // Change normal mode and upload again.
    void SetNormalMode(bool faceNormals);

    // Draw mesh triangles.
    void Draw() const;

    // Draw normal debug lines.
    void DrawNormals() const;

    // Free GPU resources.
    void Free();

    // Current buffer state.
    bool IsValid()    const { return vao_ != 0; }
    bool HasNormals() const { return normVao_ != 0; }

    // Primitive generators.
    static Mesh MakePlane();
    static Mesh MakeCube();
    static Mesh MakeCone(int slices = 4);
    static Mesh MakeCylinder(int slices = 4);
    static Mesh MakeSphere(int slices = 4, int rings = 2);

    // Load mesh from OBJ.
    static Mesh LoadOBJ(const std::string& path);

    // Save mesh to OBJ.
    void SaveOBJ(const std::string& path) const;

private:
    // Raw triangles used to rebuild buffers.
    struct RawVert { glm::vec3 pos; glm::vec2 uv; };
    struct RawTri  { RawVert v[3]; };
    std::vector<RawTri> tris_;

    // GPU handles and draw counters.
    GLuint vao_  = 0, vbo_  = 0, ebo_  = 0;
    GLuint normVao_ = 0, normVbo_ = 0;
    int    drawCount_ = 0;
    int    normCount_ = 0;
    bool   faceNormals_ = true;

    // Build vertices with face normals.
    void BuildFaceNormals(std::vector<Vertex>& verts, std::vector<unsigned int>& idx) const;

    // Build vertices with averaged normals.
    void BuildAveragedNormals(std::vector<Vertex>& verts, std::vector<unsigned int>& idx) const;

    // Upload vertices and indices.
    void UploadBuffers(const std::vector<Vertex>& verts, const std::vector<unsigned int>& idx);

    // Upload line buffer for normals.
    void UploadNormalLines(const std::vector<Vertex>& verts, float length = 0.1f);
};
