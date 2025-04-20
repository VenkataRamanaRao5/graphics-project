// main.cpp - Full Modern OpenGL App with Perspective, Navigation, and Clickable Menu (now fully modern)
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <iostream>
#include <unistd.h>

const unsigned int SCR_WIDTH = 1000;
const unsigned int SCR_HEIGHT = 600;

float camX = 0.0f, camY = 0.0f, camZ = -20.0f;
float camSpeed = 0.01f;

float centerX = 0.0f, centerY = 0.0f, centerZ = 0.0f;
float centerSpeed = 0.01f;

bool movingCam = true;

// Vertex shader for 3D rendering
const char *vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 projection;
uniform mat4 view;
void main() {
    gl_Position = projection * view * vec4(aPos, 1.0);
})";

// Fragment shader for 3D rendering
const char *fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
void main() {
    FragColor = vec4(0.2, 0.8, 1.0, 1.0);
})";

typedef std::complex<float> cfloat;
void build_buffer();
struct Poly;

std::vector<Poly *> polygons;
std::vector<float> buffer;
std::vector<Poly *> foldingWait;
unsigned int VAO, VBO;
float angle = acos(sqrt(5) / 3);

// Represents a regular polygon in 3D space (flat on XY plane)
struct Poly
{
    cfloat center;                               // Complex center of the polygon
    std::vector<glm::vec3> vertices;             // 3D vertices
    bool folded = false;                         // Indicates if the polygon has been folded in the 3D structure
    std::vector<std::vector<Poly *>> dependents; // For each edge, a list of child polygons dependent on it
    int dependentsCount = 0;

    Poly(cfloat c, int sides, float radius, float angleOffset)
    {
        center = c;
        for (int i = 0; i <= sides; ++i)
        {
            float theta = 2.0f * M_PI * i / sides + angleOffset;
            cfloat point = c + std::polar(radius, theta);
            vertices.emplace_back(point.real(), point.imag(), 0.0f);
        }
        dependents.resize(vertices.size() - 1);
    }

    Poly(Poly &parent, const std::pair<int, int> &edgeAndSides)
    {
        int edgeIndex = edgeAndSides.first;
        int sides = edgeAndSides.second;

        auto z1 = cfloat(parent.vertices[edgeIndex].x, parent.vertices[edgeIndex].y);
        auto z2 = cfloat(parent.vertices[edgeIndex - 1].x, parent.vertices[edgeIndex - 1].y);
        auto midpoint = (z1 + z2) / 2.0f;

        float theta = (sides - 2) * M_PI / (2.0 * sides);
        auto parentCenter = parent.center;
        cfloat toMid = midpoint - parentCenter;
        center = midpoint + std::tan(theta) * std::abs(z1 - z2) * 0.5f * toMid / std::abs(toMid);

        float radius = std::abs(center - z1);
        float initialAngle = std::atan2((z1 - center).imag(), (z1 - center).real());

        for (int i = 0; i <= sides; ++i)
        {
            float angle = 2.0f * M_PI * i / sides + initialAngle;
            cfloat point = center + radius * std::polar(1.0f, angle);
            vertices.emplace_back(point.real(), point.imag(), 0.0f);
        }

        dependents.resize(vertices.size() - 1);
        parent.dependents[edgeIndex - 1].push_back(this);
        parent.dependentsCount++;
    }

    // Rotate only this polygon around an axis passing through pivot point
    void foldThisOnly(float angleRad, const glm::vec3 &axis, const glm::vec3 &pivot)
    {

        glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angleRad, axis);

        for (auto &v : vertices)
        {
            glm::vec4 relative = glm::vec4(v - pivot, 1.0f);
            glm::vec4 rotated = rot * relative;
            v = glm::vec3(rotated) + pivot;
        }

        build_buffer();
    }

    void foldThisAndAll(float angleRad, const glm::vec3 &axis, const glm::vec3 &pivot)
    {
        foldThisOnly(angleRad, axis, pivot);

        for (int edge = 0; edge < dependents.size(); ++edge)
        {
            for (Poly *child : dependents[edge])
            {
                if (child->folded)
                    continue;
                child->foldThisAndAll(angleRad, axis, pivot);
            }
        }
    }

    // Recursively fold all dependent polygons
    void foldDependents(float angleRad)
    {
        folded = true;
        std ::cout << "Folding" << center << std::endl;
        for (int edge = 0; edge < dependents.size(); ++edge)
        {
            std ::cout << edge << std::endl;
            const glm::vec3 &v1 = vertices[edge];
            const glm::vec3 &v2 = vertices[edge + 1];
            glm::vec3 edgeVec = glm::normalize(v2 - v1);
            glm::vec3 pivot = 0.5f * (v1 + v2);

            for (Poly *child : dependents[edge])
            {
                if (child->folded)
                    continue;
                child->foldThisAndAll(angleRad, edgeVec, pivot);
                // child->foldDependents(angleRad);
                foldingWait.push_back(child);
            }
        }
    }
};

// Builds a flat net approximating an icosahedron
void build_icosahedron_net()
{
    angle = acos(sqrt(5) / 3);
    polygons.clear();
    foldingWait.clear();
    // Starting triangle
    polygons.emplace_back(new Poly(cfloat(0.0f, 0.0f), 3, 2.0f, M_PI / 2.0f));

    // Create net with alternating 3-2 triangle attachments
    for (int edge : {3, 2, 3, 2, 3, 2, 3, 2, 3})
    {
        polygons.emplace_back(new Poly(*polygons.back(), std::pair<int, int>{edge, 3}));
    }

    // Mirror connections
    for (int i = 0; i < 10; ++i)
    {
        int edge = (i % 2 == 0) ? 2 : 3;
        polygons.emplace_back(new Poly(*polygons[i], std::pair<int, int>{edge, 3}));
    }
    foldingWait.push_back(polygons[0]);
    // Extract line segments for rendering
    build_buffer();
}

void build_dodecahedron_net()
{

    angle = acos(1 / sqrt(5));

    polygons.clear();
    foldingWait.clear();

    polygons.emplace_back(new Poly(cfloat(0.0f, 0.0f), 5, 2.0f, M_PI / 2.0f));

    for (int edge : {1, 5, 2, 5, 2, 5, 2, 5, 2})
        polygons.emplace_back(new Poly(*polygons.back(), std::pair<int, int>{edge, 5}));

    polygons.emplace_back(new Poly(*polygons.back(), std::pair<int, int>{3, 5}));
    polygons.emplace_back(new Poly(*polygons.front(), std::pair<int, int>{3, 5}));

    foldingWait.push_back(polygons[0]);
    build_buffer();
}

void build_octahedron_net()
{
    angle = acos(1.0f / 3.0f);
    polygons.clear();
    foldingWait.clear();
    // Starting triangle
    polygons.emplace_back(new Poly(cfloat(0.0f, 0.0f), 3, 2.0f, M_PI / 2.0f));

    // Create net with alternating 3-2 triangle attachments
    for (int edge : {3, 3, 3})
    {
        polygons.emplace_back(new Poly(*polygons.back(), std::pair<int, int>{edge, 3}));
    }
    
    polygons.emplace_back(new Poly(*polygons.front(), std::pair<int, int>{2, 3}));
    for (int edge : {2, 3, 3})
    {
        polygons.emplace_back(new Poly(*polygons.back(), std::pair<int, int>{edge, 3}));
    }

    foldingWait.push_back(polygons[0]);
    // Extract line segments for rendering
    build_buffer();
}

void build_hexahedron_net()
{
    angle = M_PI / 2.0f;

    polygons.clear();
    foldingWait.clear();

    polygons.emplace_back(new Poly(cfloat(0.0f, 0.0f), 4, 2.0f, M_PI / 4.0f));

    for (int edge : {2, 3, 3})
        polygons.emplace_back(new Poly(*polygons.back(), std::pair<int, int>{edge, 4}));
        
    polygons.emplace_back(new Poly(*polygons.back(), std::pair<int, int>{4, 4}));
    polygons.emplace_back(new Poly(*polygons.front(), std::pair<int, int>{1, 4}));

    foldingWait.push_back(polygons[0]);
    build_buffer();
}

void build_tetrahedron_net()
{
    angle = acos(-1.0f / 3.0f);
    polygons.clear();
    foldingWait.clear();
    // Starting triangle
    polygons.emplace_back(new Poly(cfloat(0.0f, 0.0f), 3, 2.0f, M_PI / 2.0f));
    polygons.emplace_back(new Poly(*polygons.front(), std::pair<int, int>{1, 3}));
    polygons.emplace_back(new Poly(*polygons.front(), std::pair<int, int>{2, 3}));
    polygons.emplace_back(new Poly(*polygons.front(), std::pair<int, int>{3, 3}));
    foldingWait.push_back(polygons[0]);
    // Extract line segments for rendering
    build_buffer();
}

void build_buffer()
{
    buffer.clear();
    for (const auto &poly : polygons)
    {
        for (size_t i = 0; i < poly->vertices.size() - 1; ++i)
        {
            buffer.push_back(poly->vertices[i].x);
            buffer.push_back(poly->vertices[i].y);
            buffer.push_back(poly->vertices[i].z);
            buffer.push_back(poly->vertices[i + 1].x);
            buffer.push_back(poly->vertices[i + 1].y);
            buffer.push_back(poly->vertices[i + 1].z);
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(float), buffer.data(), GL_DYNAMIC_DRAW);
}

// Handle keyboard input for camera movement
void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
    {
        if (movingCam)
            camY += camSpeed;
        else
            centerY += centerSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
    {

        if (movingCam)
            camY -= camSpeed;
        else
            centerY -= centerSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
    {

        if (movingCam)
            camX -= camSpeed;
        else
            centerX -= centerSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
    {
        if (movingCam)
            camX += camSpeed;

        else
            centerX += centerSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
    {

        if (movingCam)
            camZ += camSpeed;
        else
            centerZ += centerSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
    {

        if (movingCam)
            camZ -= camSpeed;
        else
            centerZ -= centerSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS)
        movingCam = !movingCam;
    if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
    {
        if (movingCam)
        {
            camX = 0.0f;
            camY = 0.0f;
            camZ = -20.0f;
        }
        else
        {
            centerX = 0.0f;
            centerY = 0.0f;
            centerZ = 0.0f;
        }
    }
    static bool spaceWasPressed = false;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        spaceWasPressed = false;
    bool spacePressed = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;

    if (spacePressed && !spaceWasPressed)
    {
        Poly *poly;
        if(foldingWait.size() == 0)
            return;
        do
        {
            poly = foldingWait.front();
            foldingWait.erase(foldingWait.begin());
        } while (poly->dependentsCount == 0);
        poly->foldDependents(angle);
        spaceWasPressed = true;
    }
}

// Detect click within UI menu area
void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        if (xpos < 100)
        {
            int boxIndex = static_cast<int>(ypos / 60);
            if (boxIndex >= 0 && boxIndex < 5){
                std::cout << "Clicked on solid box: " << boxIndex << std::endl;
                switch (boxIndex)
                {
                    case 0:
                        build_tetrahedron_net();
                        break;
                    case 1:
                        build_hexahedron_net();
                        break;
                    case 2:
                        build_octahedron_net();
                        break;
                    case 3:
                        build_dodecahedron_net();
                        break;
                    case 4:
                        build_icosahedron_net();
                        break;
                }
            }
        }
    }
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Icosahedron Net", NULL, NULL);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    // Compile shaders
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Upload icosahedron geometry
    // build_icosahedron_net();

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, buffer.size() * sizeof(float), buffer.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glUseProgram(shaderProgram);

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window))
    {
        processInput(window);

        // Clear screen
        glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Set up camera and projection
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
        glm::mat4 view = glm::lookAt(glm::vec3(camX, camY, camZ), glm::vec3(centerX, centerY, centerZ), glm::vec3(0, 1, 0));

        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));

        // Draw the net
        glBindVertexArray(VAO);
        glDrawArrays(GL_LINES, 0, buffer.size() / 3);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glfwTerminate();
    return 0;
}
