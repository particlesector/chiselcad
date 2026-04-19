#include "Camera.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace chisel::render {

static constexpr float kPi    = 3.14159265358979323846f;
static constexpr float kPi2   = kPi * 2.0f;
static constexpr float kHalfPi = kPi * 0.5f;

void Camera::init(float distance) {
    m_distance = distance;
}

void Camera::onScroll(double dy) {
    m_distance -= static_cast<float>(dy) * m_distance * 0.1f;
    m_distance  = std::max(0.5f, m_distance);
}

void Camera::onMouseButton(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT)  m_leftDown  = (action == GLFW_PRESS);
    if (button == GLFW_MOUSE_BUTTON_RIGHT) m_rightDown = (action == GLFW_PRESS);
    if (!m_leftDown && !m_rightDown) m_firstMove = true;
}

void Camera::onMouseMove(double x, double y) {
    if (!m_leftDown && !m_rightDown) { m_firstMove = true; return; }
    if (m_firstMove) { m_lastX = x; m_lastY = y; m_firstMove = false; return; }

    double dx = x - m_lastX;
    double dy = y - m_lastY;
    m_lastX = x;
    m_lastY = y;

    if (m_leftDown) {
        // Orbit
        m_yaw   -= static_cast<float>(dx) * 0.005f;
        m_pitch += static_cast<float>(dy) * 0.005f;
        m_pitch  = std::clamp(m_pitch, -kHalfPi, kHalfPi);
        if (m_yaw >  kPi2) m_yaw -= kPi2;
        if (m_yaw < -kPi2) m_yaw += kPi2;
    } else if (m_rightDown) {
        // Pan in camera-local XY
        glm::mat4 v    = view();
        glm::vec3 right(v[0][0], v[1][0], v[2][0]);
        glm::vec3 up   (v[0][1], v[1][1], v[2][1]);
        float     speed = m_distance * 0.001f;
        m_target -= right * static_cast<float>(dx) * speed;
        m_target += up    * static_cast<float>(dy) * speed;
    }
}

void Camera::processInput(GLFWwindow* /*window*/, float /*dt*/) {}

void Camera::setNamedView(NamedView v) {
    switch (v) {
        // Front/Back: camera at -Y/+Y so screen-right = +X (non-mirrored)
        case NamedView::Front:     m_yaw = -kPi / 2.0f;  m_pitch =  0.0f;    break;
        case NamedView::Back:      m_yaw =  kPi / 2.0f;  m_pitch =  0.0f;    break;
        case NamedView::Right:     m_yaw =  0.0f;         m_pitch =  0.0f;    break;
        case NamedView::Left:      m_yaw =  kPi;          m_pitch =  0.0f;    break;
        // Top/Bottom: same yaw as Front for smooth arc continuity
        case NamedView::Top:       m_yaw = -kPi / 2.0f;  m_pitch =  kHalfPi; break;
        case NamedView::Bottom:    m_yaw = -kPi / 2.0f;  m_pitch = -kHalfPi; break;
        case NamedView::Isometric: m_yaw = -kPi / 4.0f;
            m_pitch = std::asin(1.0f / std::sqrt(3.0f));                      break;
    }
}

void Camera::orbitYaw(float delta) {
    m_yaw += delta;
    if (m_yaw >  kPi2) m_yaw -= kPi2;
    if (m_yaw < -kPi2) m_yaw += kPi2;
}

void Camera::shiftTargetRight(float worldAmount) {
    glm::mat4 v = view();
    glm::vec3 right(v[0][0], v[1][0], v[2][0]);
    m_target += right * worldAmount;
}

void Camera::fitToBounds(glm::vec3 minB, glm::vec3 maxB) {
    m_target = (minB + maxB) * 0.5f;
    float diagonal = glm::length(maxB - minB);
    if (diagonal < 0.001f) diagonal = 1.0f;
    float halfFov  = glm::radians(fovDeg * 0.5f);
    m_distance     = (diagonal * 0.5f) / std::tan(halfFov) * 1.25f; // 25% padding
    m_distance     = std::max(0.5f, m_distance);
}

glm::vec3 Camera::eye() const {
    // Z-up spherical coordinates: yaw = azimuth in XY plane, pitch = elevation
    float cx = std::cos(m_pitch) * std::cos(m_yaw);
    float cy = std::cos(m_pitch) * std::sin(m_yaw);
    float cz = std::sin(m_pitch);
    return m_target + glm::vec3(cx, cy, cz) * m_distance;
}

glm::mat4 Camera::view() const {
    // Orbit-arc up: tangent of the pitch arc — identical to world-Z up after
    // Gram-Schmidt for all non-degenerate pitches, but stays valid at ±90°
    // where world-Z is parallel to the view direction and lookAt degenerates.
    glm::vec3 up(-std::sin(m_pitch) * std::cos(m_yaw),
                 -std::sin(m_pitch) * std::sin(m_yaw),
                  std::cos(m_pitch));
    return glm::lookAt(eye(), m_target, up);
}

glm::mat4 Camera::projection(float aspect) const {
    glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, 0.1f, 10000.0f);
    proj[1][1] *= -1.0f;  // Vulkan NDC has Y pointing down; GLM uses OpenGL convention
    return proj;
}

glm::mat4 Camera::viewProjection(float aspect) const {
    return projection(aspect) * view();
}

} // namespace chisel::render
