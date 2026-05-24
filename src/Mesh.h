#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// -------------------------------------------------------------------------
// Per-vertex data sent to the GPU
// -------------------------------------------------------------------------
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// -------------------------------------------------------------------------
// Mesh
//   - Stores raw triangle geometry (position + UV, no normals).
//   - Normals are derived on demand in two modes:
//       face normals   : hard edges, one normal per triangle
//       averaged normals: smooth shading, unique normals averaged per position
//   - Manages VAO/VBO/EBO and a separate VAO for normal-line visualisation.
// -------------------------------------------------------------------------
class Mesh
{
public:
    Mesh()  = default;
    ~Mesh() = default;

    // Non-copyable, movable
    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&)                 = default;
    Mesh& operator=(Mesh&&)      = default;

    // Build GPU buffers. Must be called (or re-called after topology changes)
    // before Draw() / DrawNormals().
    void Upload(bool faceNormals);

    // Switch normal mode and re-upload immediately.
    void SetNormalMode(bool faceNormals);

    // Render the mesh as triangles.
    void Draw() const;

    // Render per-vertex normal lines.
    void DrawNormals() const;

    // Release all GPU resources.
    void Free();

    bool IsValid()    const { return vao_ != 0; }
    bool HasNormals() const { return normVao_ != 0; }

    // -----------------------------------------------------------------------
    // Procedural shape factories (all fit exactly in [-0.5, 0.5]^3)
    // -----------------------------------------------------------------------
    static Mesh MakePlane();
    static Mesh MakeCube();
    static Mesh MakeCone(int slices = 4);
    static Mesh MakeCylinder(int slices = 4);
    static Mesh MakeSphere(int slices = 4, int rings = 2);

    // Load a Wavefront OBJ file (positions + normals + UVs required).
    static Mesh LoadOBJ(const std::string& path);

    // Write current raw triangle data as an OBJ file.
    void SaveOBJ(const std::string& path) const;

private:
    // Raw source geometry — positions and UVs only (normals computed later)
    struct RawVert { glm::vec3 pos; glm::vec2 uv; };
    struct RawTri  { RawVert v[3]; };
    std::vector<RawTri> tris_;

    // GPU objects
    GLuint vao_  = 0, vbo_  = 0, ebo_  = 0;
    GLuint normVao_ = 0, normVbo_ = 0;
    int    drawCount_ = 0;   // number of indices to pass to glDrawElements
    int    normCount_ = 0;   // number of line vertices for DrawNormals
    bool   faceNormals_ = true;

    // Build vertex / index arrays from tris_ in either normal mode
    void BuildFaceNormals   (std::vector<Vertex>& verts, std::vector<unsigned int>& idx) const;
    void BuildAveragedNormals(std::vector<Vertex>& verts, std::vector<unsigned int>& idx) const;

    // Upload vertex+index data to a VAO
    void UploadBuffers(const std::vector<Vertex>& verts, const std::vector<unsigned int>& idx);

    // Build the normal-line visualisation buffer (line segments start=vert, end=vert+normal*len)
    void UploadNormalLines(const std::vector<Vertex>& verts, float length = 0.05f);
};
