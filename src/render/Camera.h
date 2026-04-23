#pragma once
#include <glm/glm.hpp>

struct GLFWwindow;

namespace chisel::render {

// ---------------------------------------------------------------------------
// Camera — arcball orbit.  Left-drag orbits, right-drag pans, scroll zooms.
// ---------------------------------------------------------------------------
class Camera {
public:
    enum class NamedView { Front, Back, Right, Left, Top, Bottom, Isometric };

    void init(float distance = 50.0f);
    void processInput(GLFWwindow* window, float dt);
    void onScroll(double dy);
    void onMouseButton(int button, int action);
    void onMouseMove(double x, double y);

    // Animate camera to face a named direction
    void setNamedView(NamedView v);

    // Rotate the camera around the orbit target by delta radians
    void orbitYaw(float delta);

    // Fit camera to a world-space bounding box with padding
    void fitToBounds(glm::vec3 minB, glm::vec3 maxB);

    // Shift the orbit target in the camera's right direction by worldAmount units.
    // Negative value shifts left (use to compensate for a left-side panel so the
    // mesh appears centered in the visible area rather than the full framebuffer).
    void shiftTargetRight(float worldAmount);

    float     distance() const { return m_distance; }
    float     yaw()      const { return m_yaw; }
    float     pitch()    const { return m_pitch; }
    glm::vec3 target()   const { return m_target; }

    void setState(float yaw, float pitch, float distance, glm::vec3 target);

    glm::mat4 view()       const;
    glm::mat4 projection(float aspectRatio) const;
    glm::mat4 viewProjection(float aspectRatio) const;
    glm::vec3 eye()        const;

    float fovDeg = 45.0f;

private:
    float     m_distance   = 50.0f;
    float     m_yaw        = 0.0f;     // radians — azimuth, 0 = +X direction
    float     m_pitch      = 0.4f;     // radians — elevation
    glm::vec3 m_target     = {0, 0, 0};

    bool      m_leftDown   = false;
    bool      m_rightDown  = false;
    double    m_lastX      = 0.0;
    double    m_lastY      = 0.0;
    bool      m_firstMove  = true;
};

} // namespace chisel::render
