#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// Per-vertex data sent to the GPU.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

// Mesh with primitive generation and OpenGL buffer management.
class Mesh
{
public:
    Mesh()  = default;
    ~Mesh() = default;

    // Mesh is non-copyable and movable only.
    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&)                 = default;
    Mesh& operator=(Mesh&&)      = default;

    // Builds and uploads buffers to draw the mesh.
    // faceNormals=true for hard edges, false for smooth.
    void Upload(bool faceNormals);

    // Changes normal mode and re-uploads buffers.
    void SetNormalMode(bool faceNormals);

    // Draws the mesh as triangles.
    void Draw() const;

    // Draws normal lines for debugging.
    void DrawNormals() const;

    // Releases GPU resources.
    void Free();

    // Available buffer state.
    bool IsValid()    const { return vao_ != 0; }
    bool HasNormals() const { return normVao_ != 0; }

    // Basic primitive generators (range approx [-0.5, 0.5]).
    static Mesh MakePlane();
    static Mesh MakeCube();
    static Mesh MakeCone(int slices = 4);
    static Mesh MakeCylinder(int slices = 4);
    static Mesh MakeSphere(int slices = 4, int rings = 2);

    // Loads an OBJ mesh.
    static Mesh LoadOBJ(const std::string& path);

    // Exports the current mesh to OBJ.
    void SaveOBJ(const std::string& path) const;

private:
    // Raw triangles used to rebuild buffers.
    struct RawVert { glm::vec3 pos; glm::vec2 uv; };
    struct RawTri  { RawVert v[3]; };
    std::vector<RawTri> tris_;

    // GPU objects and draw counters.
    GLuint vao_  = 0, vbo_  = 0, ebo_  = 0;
    GLuint normVao_ = 0, normVbo_ = 0;
    int    drawCount_ = 0;
    int    normCount_ = 0;
    bool   faceNormals_ = true;

    // Builds vertices with face normals.
    void BuildFaceNormals(std::vector<Vertex>& verts, std::vector<unsigned int>& idx) const;

    // Builds vertices with averaged normals.
    void BuildAveragedNormals(std::vector<Vertex>& verts, std::vector<unsigned int>& idx) const;

    // Uploads vertices and indices to the GPU.
    void UploadBuffers(const std::vector<Vertex>& verts, const std::vector<unsigned int>& idx);

    // Uploads line buffer to visualize normals.
    void UploadNormalLines(const std::vector<Vertex>& verts, float length = 0.1f);
};

