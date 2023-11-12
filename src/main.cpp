#include <iostream>
#include <vector>

#define GLEW_STATIC
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Camera.h"
#include "GLSL.h"
#include "Program.h"
#include "MatrixStack.h"
#include "Shape.h"
#include "Ship.h"
#include "Asteroid.h"

using namespace std;

#define NUM_ASTEROID_MODELS 1

GLFWwindow *window; // Main application window
string RESOURCE_DIR = ""; // Where the resources are loaded from

int keyPresses[256] = {0}; // only for English keyboards!
bool isPressed[512] = {0};
double tGlobal = 0.0f;

bool thirdPersonCam = true;
bool drawBoundingBox = true;
bool drawGrid = true;
bool drawAxisFrame = true;

shared_ptr<Program> prog;
shared_ptr<Camera> camera;
shared_ptr<Ship> shape;

vector<shared_ptr<Shape> > asteroidModels;
vector<shared_ptr<Asteroid> > asteroids;

void resetAsteroidPositions(){
	for (auto a = asteroids.begin(); a != asteroids.end(); ++a){
		(*a)->randomDir();
		(*a)->randomPos();
	}
}

static void error_callback(int error, const char *description)
{
	cerr << description << endl;
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	if(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, GL_TRUE);
	}

	if (action == GLFW_PRESS){
		isPressed[key] = true;
	}

	if (action == GLFW_RELEASE){
		isPressed[key] = false;
	}
}

static void char_callback(GLFWwindow *window, unsigned int key)
{
	keyPresses[key]++;
}

static void cursor_position_callback(GLFWwindow* window, double xmouse, double ymouse)
{
	int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
	if(state == GLFW_PRESS) {
		camera->mouseMoved(xmouse, ymouse);
	}
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	// Get the current mouse position.
	double xmouse, ymouse;
	glfwGetCursorPos(window, &xmouse, &ymouse);
	// Get current window size.
	int width, height;
	glfwGetWindowSize(window, &width, &height);
	if(action == GLFW_PRESS) {
		bool shift = mods & GLFW_MOD_SHIFT;
		bool ctrl  = mods & GLFW_MOD_CONTROL;
		bool alt   = mods & GLFW_MOD_ALT;
		camera->mouseClicked(xmouse, ymouse, shift, ctrl, alt);
	}
}

static void init()
{
	GLSL::checkVersion();
	
	// Set background color
	glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
	// Enable z-buffer test
	glEnable(GL_DEPTH_TEST);

	keyPresses[(unsigned)'c'] = 1;
	
	prog = make_shared<Program>();
	prog->setShaderNames(RESOURCE_DIR + "phong_vert.glsl", RESOURCE_DIR + "phong_frag.glsl");
	prog->setVerbose(true);
	prog->init();
	prog->addUniform("P");
	prog->addUniform("MV");
	prog->addUniform("lightPos");
	prog->addUniform("ka");
	prog->addUniform("kd");
	prog->addUniform("ks");
	prog->addUniform("s");
	prog->addAttribute("aPos");
	prog->addAttribute("aNor");
	prog->setVerbose(false);
	
	camera = make_shared<Camera>();
	camera->setInitDistance(20.0f);
	
	shape = make_shared<Ship>();
	shape->loadMesh(RESOURCE_DIR + "ship.obj");
	shape->init();

	// Initialize asteroid meshes. We only want to load them in once:
	for (int i = 0; i < NUM_ASTEROID_MODELS; i++){
		asteroidModels.push_back(make_shared<Shape>());
		asteroidModels.at(i)->loadMesh(RESOURCE_DIR + "asteroid" + to_string(i + 1) + ".obj");
		asteroidModels.at(i)->init();
	}
	
	for (int i = 0; i < NUM_ASTEROIDS; i++){
		asteroids.push_back(make_shared<Asteroid>(asteroidModels.at(i % NUM_ASTEROID_MODELS)));
	}
	
	// Initialize time.
	glfwSetTime(0.0);
	
	// If there were any OpenGL errors, this will print something.
	// You can intersperse this line in your code to find the exact location
	// of your OpenGL error.
	GLSL::checkError(GET_FILE_LINE);
}

void render()
{
	// Update time.
	double t = glfwGetTime();
	tGlobal = t;

	shape->moveShip(isPressed);
	
	// Get current frame buffer size.
	int width, height;
	glfwGetFramebufferSize(window, &width, &height);
	glViewport(0, 0, width, height);
	
	// Use the window size for camera.
	glfwGetWindowSize(window, &width, &height);
	camera->setAspect((float)width/(float)height);
	
	// Clear buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	if(keyPresses[(unsigned)'c'] % 2) {
		glEnable(GL_CULL_FACE);
	} else {
		glDisable(GL_CULL_FACE);
	}
	if(keyPresses[(unsigned)'z'] % 2) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	} else {
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	
	auto P = make_shared<MatrixStack>();
	auto MV = make_shared<MatrixStack>();
	
	// Apply camera transforms
	P->pushMatrix();
	camera->applyProjectionMatrix(P);
	MV->pushMatrix();
	camera->applyViewMatrix(MV);
	prog->bind();

	glUniformMatrix4fv(prog->getUniform("P"), 1, GL_FALSE, glm::value_ptr(P->topMatrix()));

	if (thirdPersonCam == true){
		auto E = make_shared<MatrixStack>();
		shape->applyMVTransforms(E);
		MV->rotate(M_PI, 0,1,0);
		MV->multMatrix(glm::inverse(E->topMatrix()));
	}

	for (int i = 0; i < asteroids.size(); i++){
		asteroids.at(i)->move();
		asteroids.at(i)->drawAsteroid(prog, MV);
	}

	shape->drawShip(prog, MV);
	prog->unbind();
	
	// Draw the frame and the grid with OpenGL 1.x (no GLSL)
	
	// Setup the projection matrix
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadMatrixf(glm::value_ptr(P->topMatrix()));
	
	// Setup the modelview matrix
	glMatrixMode(GL_MODELVIEW);
	
	if (drawBoundingBox){
		// Draw the ship's bounding box:
		MV->pushMatrix();
		shape->applyMVTransforms(MV);
		MV->rotate(-shape->getRoll(), 0, 0, 1);
		glPushMatrix();
		glLoadMatrixf(glm::value_ptr(MV->topMatrix()));
		shape->bb->draw();
		glPopMatrix();
		MV->popMatrix();

		// Draw each asteroid's bounding box:
		for (auto a = asteroids.begin(); a != asteroids.end(); ++a){
			MV->pushMatrix();
			(*a)->applyMVTransforms(MV);
			glPushMatrix();
			glLoadMatrixf(glm::value_ptr(MV->topMatrix()));
			(*a)->bb->draw();
			glPopMatrix();
			MV->popMatrix();
		}
	}
	// THIS NEEDS TO BE CALLED AFTER DRAWING THE SHIP AND BOUNDING BOX
	shape->updatePrevPos();

	glPushMatrix();
	glLoadMatrixf(glm::value_ptr(MV->topMatrix()));
	
	// Draw frame
	if (drawAxisFrame){
		glLineWidth(2);
		glBegin(GL_LINES);
		glColor3f(1, 0, 0);
		glVertex3f(0, 0, 0);
		glVertex3f(1, 0, 0);
		glColor3f(0, 1, 0);
		glVertex3f(0, 0, 0);
		glVertex3f(0, 1, 0);
		glColor3f(0, 0, 1);
		glVertex3f(0, 0, 0);
		glVertex3f(0, 0, 1);
		glEnd();
		glLineWidth(1);
	}
	
	// Draw grid
	if (drawGrid){
		float gridSizeHalf = 80.0f;
		int gridNx = 20;
		int gridNz = 20;

		glLineWidth(1);
		// glColor3f(0.8f, 0.8f, 0.8f);
		glColor3f(0.1f, 0.4f, 0.1f);
		glBegin(GL_LINES);
		for(int i = 0; i < gridNx+1; ++i) {
			float alpha = i / (float)gridNx;
			float x = (1.0f - alpha) * (-gridSizeHalf) + alpha * gridSizeHalf;
			glVertex3f(x, 0, -gridSizeHalf);
			glVertex3f(x, 0,  gridSizeHalf);
		}
		for(int i = 0; i < gridNz+1; ++i) {
			float alpha = i / (float)gridNz;
			float z = (1.0f - alpha) * (-gridSizeHalf) + alpha * gridSizeHalf;
			glVertex3f(-gridSizeHalf, 0, z);
			glVertex3f( gridSizeHalf, 0, z);
		}
		glEnd();
	}
	
	// Pop modelview matrix
	glPopMatrix();
	
	// Pop projection matrix
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	
	// Pop stacks
	MV->popMatrix();
	P->popMatrix();
	
	GLSL::checkError(GET_FILE_LINE);
}

int main(int argc, char **argv)
{
	if(argc < 2) {
		cout << "Please specify the resource directory." << endl;
		return 0;
	}
	RESOURCE_DIR = argv[1] + string("/");
	
	// Set error callback.
	glfwSetErrorCallback(error_callback);
	// Initialize the library.
	if(!glfwInit()) {
		return -1;
	}
	// Create a windowed mode window and its OpenGL context.
	window = glfwCreateWindow(640, 480, "OCTAVIO ALMANZA", NULL, NULL);
	if(!window) {
		glfwTerminate();
		return -1;
	}
	// Make the window's context current.
	glfwMakeContextCurrent(window);
	// Initialize GLEW.
	glewExperimental = true;
	if(glewInit() != GLEW_OK) {
		cerr << "Failed to initialize GLEW" << endl;
		return -1;
	}
	glGetError(); // A bug in glewInit() causes an error that we can safely ignore.
	cout << "OpenGL version: " << glGetString(GL_VERSION) << endl;
	cout << "GLSL version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << endl;
	// Set vsync.
	glfwSwapInterval(1);
	// Set keyboard callback.
	glfwSetKeyCallback(window, key_callback);
	// Set char callback.
	glfwSetCharCallback(window, char_callback);
	// Set cursor position callback.
	glfwSetCursorPosCallback(window, cursor_position_callback);
	// Set mouse button callback.
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	// Initialize scene.
	init();
	// Loop until the user closes the window.
	while(!glfwWindowShouldClose(window)) {
		if(!glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
			// Render scene.
			render();
			// Swap front and back buffers.
			glfwSwapBuffers(window);
		}
		// Poll for and process events.
		glfwPollEvents();
	}
	// Quit program.
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
