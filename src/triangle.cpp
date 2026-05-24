#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GL/GL.h>
#include <SDL3/SDL.h>

#include "OGLDebug.h"

static int     winID;
static GLsizei WIDTH = 1280;
static GLsizei HEIGHT = 720;

GLuint CreateShader(GLenum eShaderType, const std::string& strShaderFile)
{
	GLuint       shader = glCreateShader(eShaderType);
	const char* strFileData = strShaderFile.c_str();
	glShaderSource(shader, 1, &strFileData, NULL);

	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint infoLogLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);

		GLchar* strInfoLog = new GLchar[infoLogLength + 1];
		glGetShaderInfoLog(shader, infoLogLength, NULL, strInfoLog);

		const char* strShaderType = NULL;
		switch (eShaderType)
		{
		case GL_VERTEX_SHADER:
			strShaderType = "vertex";
			break;
		case GL_GEOMETRY_SHADER:
			strShaderType = "geometry";
			break;
		case GL_FRAGMENT_SHADER:
			strShaderType = "fragment";
			break;
		}

		fprintf(stderr, "Compile failure in %s shader:\n%s\n", strShaderType, strInfoLog);
		delete[] strInfoLog;
	}

	return shader;
}

GLuint CreateProgram(const std::vector<GLuint>& shaderList)
{
	GLuint program = glCreateProgram();

	for (size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
		glAttachShader(program, shaderList[iLoop]);

	glLinkProgram(program);

	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint infoLogLength;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);

		GLchar* strInfoLog = new GLchar[infoLogLength + 1];
		glGetProgramInfoLog(program, infoLogLength, NULL, strInfoLog);
		fprintf(stderr, "Linker failure: %s\n", strInfoLog);
		delete[] strInfoLog;
	}

	for (size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
		glDetachShader(program, shaderList[iLoop]);

	return program;
}

const std::string strVertexShader = R"(
	#version 430
	layout(location = 0) in vec4 aPosition;
	layout(location = 1) in vec3 aColor;
	out vec3 color;
	void main()
	{
	   gl_Position = aPosition;
	   color = aColor;
	})";

const std::string strFragmentShader = R"(
	#version 430
	in vec3 color;
	out vec4 outputColor;
	void main()
	{
	   outputColor = vec4(color, 1.0f);
	})";

struct Vertex
{
	float pos[4];
	float color[3];
};

const Vertex vertexData[3] = {
	{{0.75f, 0.75f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
	{{0.75f, -0.75f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
	{{-0.75f, -0.75f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}} };

namespace
{
	GLuint theProgram;
	GLuint vertexBufferObject;
	GLuint vao;
}

void InitializeProgram()
{
	std::vector<GLuint> shaderList;

	shaderList.push_back(CreateShader(GL_VERTEX_SHADER, strVertexShader));
	shaderList.push_back(CreateShader(GL_FRAGMENT_SHADER, strFragmentShader));

	theProgram = CreateProgram(shaderList);

	std::for_each(shaderList.begin(), shaderList.end(), glDeleteShader);
}


void InitializeBuffers()
{
	// VAO
	glGenVertexArrays(1, &vao);

	// VBO
	glGenBuffers(1, &vertexBufferObject);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBufferObject);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertexData), vertexData, GL_STATIC_DRAW);

	// Insert the VBO into the VAO
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), 0);

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));

	// Unbind
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

//Called after the window and OpenGL are initialized. Called exactly once, before the main loop.
void init()
{
	InitializeProgram();
	InitializeBuffers();
}

//Called to update the display.
//You should call SDL_GL_SwapWindow after all of your rendering to display what you rendered.
void display(SDL_Window* window)
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Bind the glsl program and this object's VAO
	glUseProgram(theProgram);
	glBindVertexArray(vao);

	// Draw
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// Unbind
	glBindVertexArray(0);
	glUseProgram(0);

	SDL_GL_SwapWindow(window);
}

void cleanup()
{
	// Delete the program
	glDeleteProgram(theProgram);
	// Delete the VBOs
	glDeleteBuffers(1, &vertexBufferObject);
	// Delete the VAO
	glDeleteVertexArrays(1, &vao);
}

int main(int argc, char* args[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		std::cout << "Could not initialize SDL: " << SDL_GetError() << std::endl;
		exit(1);
	}

	SDL_Window* window = SDL_CreateWindow("CS300", WIDTH, HEIGHT, SDL_WINDOW_OPENGL);
	if (window == nullptr)
	{
		std::cout << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
		SDL_Quit();
		exit(1);
	}

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_GLContext context_ = SDL_GL_CreateContext(window);
	if (context_ == nullptr)
	{
		SDL_DestroyWindow(window);
		std::cout << "SDL_GL_CreateContext Error: " << SDL_GetError() << std::endl;
		SDL_Quit();
		exit(1);
	}

	glewExperimental = true;
	if (glewInit() != GLEW_OK)
	{
		SDL_GL_DestroyContext(context_);
		SDL_DestroyWindow(window);
		std::cout << "GLEW Error: Failed to init" << std::endl;
		SDL_Quit();
		exit(1);
	}

#if _DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(MessageCallback, 0);
#endif

	// print GPU data
	std::cout << "GL_VENDOR: " << glGetString(GL_VENDOR) << std::endl;
	std::cout << "GL_RENDERER: " << glGetString(GL_RENDERER) << std::endl;
	std::cout << "GL_VERSION: " << glGetString(GL_VERSION) << std::endl;

	GLint totalMemKb = 0;
	glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &totalMemKb);
	std::cout << "GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX: " << totalMemKb << std::endl;
	glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemKb);
	std::cout << "GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX: " << totalMemKb << std::endl;

	std::cout << std::endl
		<< "Extensions: "
		<< std::endl;
	int numExtensions;
	glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
	for (int i = 0; i < numExtensions; i++)
	{
		std::cout << glGetStringi(GL_EXTENSIONS, i) << std::endl;
	}

	init();

	SDL_Event event;
	bool      quit = false;
	while (!quit)
	{
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_EVENT_QUIT:
				quit = true;
				break;
			case SDL_EVENT_KEY_DOWN:
				if (event.key.type == SDL_SCANCODE_ESCAPE)
					quit = true;
				break;
			}
		}

		display(window);
	}

	cleanup();

	SDL_GL_DestroyContext(context_);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}