// Update animations
float time = static_cast<float>(FRC::GetInstance()->GetTime());

// obj contains the information for the object to render using 2 positions
// pos: stores the original position in the scene file (does not change)
// currPos: stores the position of the object in this frame
obj.currPos = obj.pos;
for (size_t u = 0; u < obj.animations.size(); u++)
{
    const Animations::Anim & anim = obj.animations[u];
    obj.currPos = anim.Update(obj.currPos, time);
}
