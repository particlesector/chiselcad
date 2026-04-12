#include "Camera.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace chisel::render {

static constexpr float kPi    = 3.14159265358979323846f;
static constexpr float kPi2   = kPi * 2.0f;
static constexpr float kHalfPi = kPi * 0.5f - 0.01f;

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

glm::vec3 Camera::eye() const {
    float cx = std::cos(m_pitch) * std::cos(m_yaw);
    float cy = std::sin(m_pitch);
    float cz = std::cos(m_pitch) * std::sin(m_yaw);
    return m_target + glm::vec3(cx, cy, cz) * m_distance;
}

glm::mat4 Camera::view() const {
    return glm::lookAt(eye(), m_target, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(glm::radians(fovDeg), aspect, 0.1f, 10000.0f);
}

glm::mat4 Camera::viewProjection(float aspect) const {
    return projection(aspect) * view();
}

} // namespace chisel::render
