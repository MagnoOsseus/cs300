/* ---------------------------------------------------------------------------------------------------------
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior written
consent of DigiPen Institute of Technology is prohibited.
Project: cs300_davidalejandro.s_1
Author: David Alejandro Garcia
Creation date: 14/06/2026
----------------------------------------------------------------------------------------------------------*/
•	Select shape to be rendered through the number keys.
    o	Numbers 1 to 5: Change the shape to be rendered
        1: Plane
        2: Cube
        3: Cone
        4: Cylinder
        5: Sphere
•	+: Increase the shape subdivisions
•	-: Decrease the shape subdivisions

•	P: Toggle to pause/start the light animation. 
•	N: Toggle normal rendering
•	T: Toggle texture-mapping on/off
•	F: Toggle face/averaged normal
•	M: Toggle wireframe mode on/off

•	Object rotation for center shape.
    o	Arrows Up/Down: Rotate the shape along Y-axis
    o	Arrows Right/Left: Rotate the shape along X-axis

It is implemented in src/main.cpp and data/shaders/phong.frag: in main.cpp, the light uniforms (uLight[], uLightNum, uCameraPos, uAmbientBoost, etc.) are loaded and sent to the main shader every frame.
The actual calculation is performed in phong.frag, where ambient, diffuse, and specular (Phong) are combined per fragment, and distance attenuation and a focus factor are also applied for SPOT lights.
