
//----------------------------------------------------------------------------
// FRAGMENT OR VERTEX SHADER
//----------------------------------------------------------------------------

// struct definition
struct Light
{
    int  type;
    
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    
    vec3 position;
    //  other variables 
    //  ...
};


// uniform declarations

uniform int   uLightNum;
uniform Light uLight[LIGHT_NUM_MAX];



//----------------------------------------------------------------------------
// OPENGL application code
//----------------------------------------------------------------------------

// We need to retrieve individually the location for each element of the array,
// for each element of the structure
program.SetUniform("uLightNum", int(lights.size()));
for (size_t i = 0; i < lights.size(); i++)
{
	program.SetUniform("uLight[" + std::to_string(i) + "].type", int(lights[i]->type));
    
	program.SetUniform("uLight[" + std::to_string(i) + "].ambient", lights[i]->ambientColor);
	program.SetUniform("uLight[" + std::to_string(i) + "].diffuse", lights[i]->diffuseColor);
	program.SetUniform("uLight[" + std::to_string(i) + "].specular", lights[i]->specularColor);
    
	program.SetUniform("uLight[" + std::to_string(i) + "].positionWorld", lights[i]->positionWorld);

    //  other variables 
    //  ...
}
