#pragma once
#include <glm/glm.hpp>

struct GLFWwindow;

namespace chisel::render {

// ---------------------------------------------------------------------------
// Camera — arcball orbit.  Left-drag orbits, right-drag pans, scroll zooms.
// ---------------------------------------------------------------------------
class Camera {
public:
    void init(float distance = 50.0f);
    void processInput(GLFWwindow* window, float dt);
    void onScroll(double dy);
    void onMouseButton(int button, int action);
    void onMouseMove(double x, double y);

    glm::mat4 view()       const;
    glm::mat4 projection(float aspectRatio) const;
    glm::mat4 viewProjection(float aspectRatio) const;
    glm::vec3 eye()        const;

    float fovDeg = 45.0f;

private:
    float     m_distance   = 50.0f;
    float     m_yaw        = 0.0f;     // radians
    float     m_pitch      = 0.4f;     // radians
    glm::vec3 m_target     = {0, 0, 0};

    bool      m_leftDown   = false;
    bool      m_rightDown  = false;
    double    m_lastX      = 0.0;
    double    m_lastY      = 0.0;
    bool      m_firstMove  = true;
};

} // namespace chisel::render
