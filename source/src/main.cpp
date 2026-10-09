// THIS IS THE FILE YOU MUST START FROM!

// This has been adapted from the Vulkan tutorial
#include <sstream>
#include <cmath>
#include <fstream>

#include <json.hpp>

#include "modules/Starter.hpp"
#include "modules/TextMaker.hpp"
// Colliders.hpp 没有 include guard，而 Scene.hpp 内部已经 #include 过一次了，
// 这里不能再显式 include 一遍，不然 Collider/AABBextents 这些类型会被定义两次报错。
// Scene.hpp 引入之后，Collider 这些类型在下面已经可以直接用了。
#include "modules/Scene.hpp"

// The uniform buffer object used in this example
struct UniformBufferObject {
	alignas(16) glm::mat4 mvpMat;
	alignas(16) glm::mat4 mMat;
	alignas(16) glm::mat4 nMat;
	alignas(16) glm::vec4 specular;
	alignas(16) glm::vec3 ambient;
	alignas(16) glm::vec4 highlight;
};

struct GlobalUniformBufferObject {
	alignas(16) glm::vec3 lightDir;
	alignas(16) glm::vec3 lightColor;
	alignas(16) glm::vec3 eyePos;
	alignas(16) glm::vec3 ambientLight;
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 norm;
	glm::vec2 UV;
};


struct IconVertex {
	glm::vec2 pos;
	glm::vec2 uv;
};


// Data structure of furniture system-----

// enum type: furniture types: same order for iconFiles[]、DEFAULT_SPAWN_POS[]
enum FurnitureType {
	DESK, CHAIR, BLACKBOARD, SHELF, PODIUM, LOCKER,
	DESK2, CHAIR2, DESK3, CHAIR3, PLANT1, PLANT2, PAPER, CLOCK, NOTICEBOARD, RACK2, BOOK, LAPTOP,
	FURNITURE_TYPE_COUNT
};

// to store the corresponding type name of the enum type
const char* FurnitureTypeNames[FURNITURE_TYPE_COUNT] = {
	"DESK", "CHAIR", "BLACKBOARD", "SHELF", "PODIUM", "LOCKER",
	"DESK2", "CHAIR2", "DESK3", "CHAIR3", "PLANT1", "PLANT2",
	"PAPER", "CLOCK", "NOTICEBOARD", "RACK2", "BOOK", "LAPTOP"
};

// Maximum number of pre-allocated Vulkan resources for each furniture
const int MAX_DESKS       = 30;
const int MAX_CHAIRS      = 30;
const int MAX_BLACKBOARDS = 5;
const int MAX_SHELVES     = 15;
const int MAX_PODIUMS     = 5;
const int MAX_LOCKERS     = 10;
const int MAX_DESK2S      = 25;
const int MAX_CHAIR2S     = 25;
const int MAX_DESK3S      = 25;
const int MAX_CHAIR3S     = 25;
const int MAX_PLANT1S     = 15;
const int MAX_PLANT2S     = 15;
const int MAX_PAPERS      = 20;
const int MAX_CLOCKS      = 10;
const int MAX_NOTICEBOARDS= 10;
const int MAX_RACK2S      = 20;
const int MAX_BOOKS       = 30;
const int MAX_LAPTOPS     = 30;

// total slots for pre-allocated space of Descriptor Pool
const int TOTAL_FURNITURE_SLOTS = MAX_DESKS + MAX_CHAIRS + MAX_BLACKBOARDS + MAX_SHELVES + MAX_PODIUMS + MAX_LOCKERS
	+ MAX_DESK2S + MAX_CHAIR2S + MAX_DESK3S + MAX_CHAIR3S + MAX_PLANT1S + MAX_PLANT2S
	+ MAX_PAPERS + MAX_CLOCKS + MAX_NOTICEBOARDS + MAX_RACK2S + MAX_BOOKS + MAX_LAPTOPS;

// the boundaries of room
const float ROOM_MIN_X = -7.5f, ROOM_MAX_X = 7.5f;
const float ROOM_MIN_Z = -12.0f, ROOM_MAX_Z = 0.0f;
const float ROOM_MIN_Y =  0.0f, ROOM_MAX_Y =  3.0f;
const float WALL_THICKNESS_MARGIN = 0.15f;  //the thickness of wall


// // Each slot stores whether a furniture instance is active, position, and rotation
struct FurnitureInstance {
	bool  active = false;
	glm::vec3 pos = glm::vec3(0.0f);  //  Position in world coordinates
	float rotY = 0.0f;
};

//Stores all data required by one furniture type: the model of this furniture, a set of descriptor
struct FurnitureTypeData {
	Model model;
	std::vector<DescriptorSet> DS;
	std::vector<FurnitureInstance> inst;
	glm::mat4 baseRot = glm::mat4(1.0f);  // correct the original orientation of the model

	//  Half size of this furniture type along the X and Z axe,to avoid the object go through the wall
	float halfX = 0.0f;
	float halfZ = 0.0f;
	// adjust the scale of this furniture(book plant)
	float baseScale = 1.0f;
};


// MAIN !

class ClassroomPlanner : public BaseProject {
	protected:
	// Here you list all the Vulkan objects you need:
	
	// Descriptor Layouts [what will be passed to the shaders]
	DescriptorSetLayout DSLlocal, DSLglobal;

	// Vertex formants, Pipelines [Shader couples] and Render passes
	VertexDescriptor VD;
	RenderPass RP;
	Pipeline P;

	// Models, textures and Descriptors (values assigned to the uniforms)
	DescriptorSet DSglobal;
	// ---Furniture system
	Texture TSchool;
	FurnitureTypeData Desk, Chair, Blackboard, Shelf, Podium, Locker, Desk2, Chair2, Desk3, Chair3, Plant1, Plant2, Paper, Clock, NoticeBoard, Rack2, Book, Laptop;
	// Lookup table of pointers to all furniture types
	FurnitureTypeData* furnitureTypes[FURNITURE_TYPE_COUNT];

	// --- furniture interaction( for the selected furniture
	int selectedType  = -1;
	int selectedIndex = -1; // index in slots

	// ----icon UI
	// A separate 2D rendering pipeline:
	static const int ICON_COUNT = FURNITURE_TYPE_COUNT;
	VertexDescriptor VDIcon;
	DescriptorSetLayout DSLIcon;
	RenderPass RPIcon;
	Pipeline PIcon;

	Texture TIcons[ICON_COUNT];
	DescriptorSet DSIcon[ICON_COUNT];
	Model IconModel;
	// NDC coordinate in the screen
	glm::vec2 iconCenter[ICON_COUNT];
	float iconHalfW, iconHalfH;

	// if window size changes, icon layout will be rebuilt in updateUniformBuffer()
	bool iconLayoutDirty = false;

	// To support loading assets from a scene.json file
	Scene SC;
	std::vector<VertexDescriptorRef>  VDRs;
	std::vector<TechniqueRef> PRs;

	// to provide textual feedback
	TextMaker txt;
	
	// Other application parameters
	float Ar;	// Aspect ratio

	glm::mat4 ViewPrj;
	glm::mat4 View;
	
	// Here you set the main application parameters
	void setWindowParameters() {
		// window size, titile and initial background
		windowWidth = 800;
		windowHeight = 600;
		windowTitle = "Classroom Planner";
    	windowResizable = GLFW_TRUE;
		
		// Initial aspect ratio
		Ar = 4.0f / 3.0f;
	}
	
	// What to do when the window changes size
	void onWindowResize(int w, int h) {
		std::cout << "Window resized to: " << w << " x " << h << "\n";
		//Aspect Ratio
		Ar = (float)w / (float)h;
		// Update Render Pass
		RP.width = w;
		RP.height = h;
		RPIcon.width = w;
		RPIcon.height = h;
		iconLayoutDirty = true;

		// updates the textual output
		txt.resizeScreen(w, h);
	}
	
	// Here you load and setup all your Vulkan Models and Texutures.
	// Here you also create your Descriptor set layouts and load the shaders for the pipelines
	void localInit() {
		// Descriptor Layouts [what will be passed to the shaders]
		// 1. Define descriptor set layouts and vertex input format
		DSLlocal.init(this, {
					// this array contains the binding:
					// first  element : the binding number
					// second element : the type of element (buffer or texture)
					// third  element : the pipeline stage where it will be used
					{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(UniformBufferObject), 1},
					{1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}
				  });
		DSLglobal.init(this, {
					// this array contains the binding:
					// first  element : the binding number
					// second element : the type of element (buffer or texture)
					// third  element : the pipeline stage where it will be used
					{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, sizeof(GlobalUniformBufferObject), 1}
				  });
		//vertex Descriptor
		VD.init(this, {
						{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}
					}, {
						{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos),
							   sizeof(glm::vec3), POSITION},
						{0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, norm),
							   sizeof(glm::vec3), NORMAL},
						{0, 2, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, UV),
							   sizeof(glm::vec2), UV}
					});

		// 2. Load furniture models and the shared texture
		Desk.model.init(this, &VD, "assets/models/School/SM_School_ComputerClass_Desk01.mgcg", MGCG);
		Chair.model.init(this, &VD, "assets/models/School/SM_School_ComputerClass_Chair01.mgcg", MGCG);
		Blackboard.model.init(this, &VD, "assets/models/School/SM_School_BlackBoard.mgcg", MGCG);
		Shelf.model.init(this, &VD, "assets/models/School/SM_School_Library_Rack01.mgcg", MGCG);
		Podium.model.init(this, &VD, "assets/models/School/SM_School_Props_Podium.mgcg", MGCG);
		Locker.model.init(this, &VD, "assets/models/School/SM_School_Props_Locker.mgcg", MGCG);
		Desk2.model.init(this, &VD, "assets/models/School/SM_School_Props_ClassTable_02.mgcg", MGCG);
		Chair2.model.init(this, &VD, "assets/models/School/SM_School_Props_ClassChair02.mgcg", MGCG);
		Desk3.model.init(this, &VD, "assets/models/School/SM_School_Props_Desk03.mgcg", MGCG);
		Chair3.model.init(this, &VD, "assets/models/School/SM_School_Props_RollingChair01.mgcg", MGCG);
		Plant1.model.init(this, &VD, "assets/models/School/SM_School_Env_Pot_01.mgcg", MGCG);
		Plant2.model.init(this, &VD, "assets/models/School/SM_School_Env_Pot_03.mgcg", MGCG);
		Paper.model.init(this, &VD, "assets/models/School/SM_School_Paper01.mgcg", MGCG);
		Clock.model.init(this, &VD, "assets/models/School/SM_School_Props_Clock.mgcg", MGCG);
		NoticeBoard.model.init(this, &VD, "assets/models/School/SM_School_NoticeBoard.mgcg", MGCG);
		Rack2.model.init(this, &VD, "assets/models/School/SM_School_Library_Rack02.mgcg", MGCG);
		Book.model.init(this, &VD, "assets/models/School/SM_School_Library_Book01.mgcg", MGCG);
		Laptop.model.init(this, &VD, "assets/models/School/SM_School_ComputerClass_Laptop01.mgcg", MGCG);

		// Texture
		TSchool.init(this, "assets/textures/School/Tex_School.png");


		// 3. Pre-allocate furniture instance slots and Descriptor Sets
		Desk.DS.resize(MAX_DESKS);             Desk.inst.resize(MAX_DESKS);
		Chair.DS.resize(MAX_CHAIRS);           Chair.inst.resize(MAX_CHAIRS);
		Blackboard.DS.resize(MAX_BLACKBOARDS); Blackboard.inst.resize(MAX_BLACKBOARDS);
		Shelf.DS.resize(MAX_SHELVES);          Shelf.inst.resize(MAX_SHELVES);
		Podium.DS.resize(MAX_PODIUMS);         Podium.inst.resize(MAX_PODIUMS);
		Locker.DS.resize(MAX_LOCKERS);         Locker.inst.resize(MAX_LOCKERS);
		Desk2.DS.resize(MAX_DESK2S);           Desk2.inst.resize(MAX_DESK2S);
		Chair2.DS.resize(MAX_CHAIR2S);         Chair2.inst.resize(MAX_CHAIR2S);
		Desk3.DS.resize(MAX_DESK3S);           Desk3.inst.resize(MAX_DESK3S);
		Chair3.DS.resize(MAX_CHAIR3S);         Chair3.inst.resize(MAX_CHAIR3S);
		Plant1.DS.resize(MAX_PLANT1S);         Plant1.inst.resize(MAX_PLANT1S);
		Plant2.DS.resize(MAX_PLANT2S);         Plant2.inst.resize(MAX_PLANT2S);
		Paper.DS.resize(MAX_PAPERS);           Paper.inst.resize(MAX_PAPERS);
		Clock.DS.resize(MAX_CLOCKS);           Clock.inst.resize(MAX_CLOCKS);
		NoticeBoard.DS.resize(MAX_NOTICEBOARDS); NoticeBoard.inst.resize(MAX_NOTICEBOARDS);
		Rack2.DS.resize(MAX_RACK2S);           Rack2.inst.resize(MAX_RACK2S);
		Book.DS.resize(MAX_BOOKS);             Book.inst.resize(MAX_BOOKS);
		Laptop.DS.resize(MAX_LAPTOPS);         Laptop.inst.resize(MAX_LAPTOPS);


		// 4. Build the lookup table for all furniture types
		furnitureTypes[DESK]       = &Desk;
		furnitureTypes[CHAIR]      = &Chair;
		furnitureTypes[BLACKBOARD] = &Blackboard;
		furnitureTypes[SHELF]      = &Shelf;
		furnitureTypes[PODIUM]     = &Podium;
		furnitureTypes[LOCKER]     = &Locker;
		furnitureTypes[DESK2]       = &Desk2;
		furnitureTypes[CHAIR2]      = &Chair2;
		furnitureTypes[DESK3]       = &Desk3;
		furnitureTypes[CHAIR3]      = &Chair3;
		furnitureTypes[PLANT1]      = &Plant1;
		furnitureTypes[PLANT2]      = &Plant2;
		furnitureTypes[PAPER]       = &Paper;
		furnitureTypes[CLOCK]       = &Clock;
		furnitureTypes[NOTICEBOARD] = &NoticeBoard;
		furnitureTypes[RACK2]       = &Rack2;
		furnitureTypes[BOOK]        = &Book;
		furnitureTypes[LAPTOP]      = &Laptop;

		// 5. Correct the base orientation and scale of the furniture models
		glm::mat4 standUpFix = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(1, 0, 0));
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
			furnitureTypes[t]->baseRot = standUpFix;
		}
		Plant1.baseScale = 2.2f;
		Plant2.baseScale = 4.0f;

		// 6. Compute furniture bounding sizes for boundary constraints
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
			FurnitureTypeData *fd = furnitureTypes[t];
			Collider c;
			c.fitAABB(&fd->model);
			c.setWorldMatrix(fd->baseRot);
			AABBextents E = c.getExtents();
			fd->halfX = (E.xMax - E.xMin) / 2.0f * fd->baseScale;
			fd->halfZ = (E.zMax - E.zMin) / 2.0f * fd->baseScale;
		}

		// 7. Initialize the default classroom furniture layout
		{
			const float rowZ[2] = { -9.5f, -6.5f };
			const float colX[3] = { -4.5f, 0.0f, 4.5f };
			int slot = 0;
			for (int row = 0; row < 2; row++) {
				for (int col = 0; col < 3; col++) {
					Desk.inst[slot].active = true;
					Desk.inst[slot].pos    = glm::vec3(colX[col], 0.0f, rowZ[row]);

					Chair.inst[slot].active = true;
					Chair.inst[slot].pos    = glm::vec3(colX[col], 0.0f, rowZ[row]+ 0.5f);
					Chair.inst[slot].rotY = glm::radians(180.0f);

					slot++;
				}
			}
		}
		//Blackboard
		Blackboard.inst[0].active = true;
		Blackboard.inst[0].pos    = glm::vec3(0.0f, 1.0f, -11.8f);
		//shelf
		Shelf.inst[0].active = true;
		Shelf.inst[0].pos    = glm::vec3(-7.2f, 0.0f, -10.0f);
		Shelf.inst[0].rotY   = glm::radians(90.0f);

		// podium
		Podium.inst[0].active = true;
		Podium.inst[0].pos    = glm::vec3(2.8f, 0.0f, -10.5f);

		Locker.inst[0].active = true;
		Locker.inst[0].pos    = glm::vec3(7.0f, 0.0f, -10.0f);
		Locker.inst[0].rotY   = glm:: radians(270.0f);
		Locker.inst[1].active = true;
        Locker.inst[1].pos    = glm::vec3(7.0f, 0.0f, -9.0f);
		Locker.inst[1].rotY   = glm:: radians(270.0f);

		Plant2.inst[0].active = true;
		Plant2.inst[0].pos    = glm::vec3(-6.5f, 0.0f, -11.2f);



		// 8. Initialize the main 3D RenderPass and graphics pipeline
		RP.init(this);
		// sets the blue sky
		RP.properties[0].clearValue = {0.0f,0.9f,1.0f,1.0f};

		P.init(this, &VD, "shaders/PosNormUV.vert.spv",
			  "shaders/LambertBlinn.frag.spv",
			  {&DSLglobal, &DSLlocal});


		// 9. Initialize the 2D icon-bar rendering system

		//vertex format
		VDIcon.init(this, {
						{0, sizeof(IconVertex), VK_VERTEX_INPUT_RATE_VERTEX}
					}, {
						{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(IconVertex, pos),
							   sizeof(glm::vec2), OTHER},
						{0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(IconVertex, uv),
							   sizeof(glm::vec2), UV}
					});

		DSLIcon.init(this, {
						{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}
					});


		{
			const char* iconFiles[ICON_COUNT] = {
				"assets/models/School/SM_School_ComputerClass_Desk01.png",  // DESK
				"assets/models/School/SM_School_ComputerClass_Chair01.png", // CHAIR
				"assets/models/School/SM_School_BlackBoard.png",            // BLACKBOARD
				"assets/models/School/SM_School_Library_Rack01.png",        // SHELF
				"assets/models/School/SM_School_Props_Podium.png",          // PODIUM
				"assets/models/School/SM_School_Props_Locker.png",          // LOCKER
				"assets/models/School/SM_School_Props_ClassTable_02.png",   // DESK2
				"assets/models/School/SM_School_Props_ClassChair02.png",    // CHAIR2
				"assets/models/School/SM_School_Props_Desk03.png",          // DESK3
				"assets/models/School/SM_School_Props_RollingChair01.png",  // CHAIR3
				"assets/models/School/SM_School_Env_Pot_01.png",            // PLANT1
				"assets/models/School/SM_School_Env_Pot_03.png",            // PLANT2
				"assets/models/School/SM_School_Paper01.png",               // PAPER
				"assets/models/School/SM_School_Props_Clock.png",           // CLOCK
				"assets/models/School/SM_School_NoticeBoard.png",           // NOTICEBOARD
				"assets/models/School/SM_School_Library_Rack02.png",        // RACK2
				"assets/models/School/SM_School_Library_Book01.png",        // BOOK
				"assets/models/School/SM_School_ComputerClass_Laptop01.png",// LAPTOP
			};
			for (int i = 0; i < ICON_COUNT; i++) TIcons[i].init(this, iconFiles[i]);
		}

		// Initialize the RenderPass and graphics pipeline for the icon bar
		RPIcon.init(this, -1, -1, -1,
					RenderPass::getStandardAttchmentsProperties(AT_SURFACE_NOAA_DEPTH, this));
		RPIcon.properties[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		RPIcon.properties[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		RPIcon.properties[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		RPIcon.properties[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		PIcon.init(this, &VDIcon, "shaders/Icon.vert.spv", "shaders/Icon.frag.spv", {&DSLIcon});
		PIcon.setCullMode(VK_CULL_MODE_NONE);
		PIcon.setTransparency(true);

		PIcon.setCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

		// Update the aspect ratio and build the icon-bar mesh
		{
			int fbW, fbH;
			glfwGetFramebufferSize(window, &fbW, &fbH);
			if (fbW > 0 && fbH > 0) Ar = (float)fbW / (float)fbH;
		}

		//
		buildIconMesh();

		// 10. Configure the Descriptor Pool capacity
		DPSZs.uniformBlocksInPool = 2  + TOTAL_FURNITURE_SLOTS;               // 原有(全局+地板) + 家具槽位数
		DPSZs.texturesInPool      = 1  + TOTAL_FURNITURE_SLOTS + ICON_COUNT;  // 原有(地板贴图) + 家具槽位数 + 每个图标各一张贴图
		DPSZs.setsInPool          = 2  + TOTAL_FURNITURE_SLOTS + ICON_COUNT;  // 原有 + 家具槽位数 + 每个图标各一个 DescriptorSet

		//11. Configure the vertex format and rendering technique used by the Scene system
		// then load the static scene from scene.json.
		VDRs.resize(1);
		VDRs[0].init("VDposNormUV", &VD);

		PRs.resize(1);
		PRs[0].init("BlinnPos", {
							{&P, {//Pipeline and DSL for the main pass
							 /*DSLglobal*/{},
							 /*DSLlocal*/{
									/*t0*/{true,  0, {}}
								  }
								 }
								}
						  }, /*TotalNtextures*/1, &VD);

		if(SC.init(this, 1, VDRs, PRs, "assets/scenes/scene.json") != 0) {
			std::cout << "ERROR LOADING THE SCENE\n";
			exit(0);
		}

		// 12. Initialize the text rendering system using the actual framebuffer size
		{
			int fbW, fbH;
			glfwGetFramebufferSize(window, &fbW, &fbH);
			txt.init(this, fbW, fbH);// Initialize
		}

		// 13. Register the main and icon-bar command buffers
		submitCommandBuffer("main", 0, populateCommandBufferAccess, this);
		submitCommandBuffer("iconbar", 5000, populateIconCommandBufferAccess, this);

		// 14. Initialize the FPS counter and control instructions
		txt.print(1.0f, -1.0f, "FPS:",1,"CO",false,false,true,TAL_RIGHT,TRH_RIGHT,TRV_TOP,{1.0f,0.0f,0.0f,1.0f},{0.8f,0.8f,0.0f,1.0f});

		txt.print(1.0f, 0.55f,
			"Camera: WASD move / Arrows or drag rotate / R,F zoom\n"
			"Click icon to spawn / Right-click to select\n"
			"Selected object: IJKL move / U,O up-down / Q,E rotate / Del delete\n"
			"F5 save layout / F9 load layout",
			2, "CO", false, false, true, TAL_RIGHT, TRH_RIGHT, TRV_TOP,
			{1.0f,1.0f,1.0f,1.0f}, {0.0f,0.0f,0.0f,1.0f});

	}
	
	// Here you create your pipelines and Descriptor Sets!
	void pipelinesAndDescriptorSetsInit() {
		// 1. Create the main RenderPass and graphics pipeline
		RP.create();
		P.create(&RP);

		// 2. Create the global Descriptor Set
		DSglobal.init(this, &DSLglobal, {});

		// 3. Create one local Descriptor Set for each furniture slot
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++)
			for (auto &ds : furnitureTypes[t]->DS)
				ds.init(this, &DSLlocal, {TSchool.getViewAndSampler()});

		// 4. Create the icon-bar RenderPass, pipeline, and Descriptor Sets
		RPIcon.create();
		PIcon.create(&RPIcon);
		for (int i = 0; i < ICON_COUNT; i++)
			DSIcon[i].init(this, &DSLIcon, {TIcons[i].getViewAndSampler()});

		// 5. Initialize the rendering resources required by the Scene and Text systems
		SC.pipelinesAndDescriptorSetsInit();
		txt.pipelinesAndDescriptorSetsInit();
	}

	// Here you destroy your pipelines and Descriptor Sets!
	void pipelinesAndDescriptorSetsCleanup() {
		P.cleanup();

		RP.cleanup();
		
		DSglobal.cleanup();

		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++)
			for (auto &ds : furnitureTypes[t]->DS)
				ds.cleanup();

		PIcon.cleanup();
		RPIcon.cleanup();
		for (int i = 0; i < ICON_COUNT; i++) DSIcon[i].cleanup();

		SC.pipelinesAndDescriptorSetsCleanup();
		txt.pipelinesAndDescriptorSetsCleanup();
	}

	// Here you destroy all the Models, Texture and Desc. Set Layouts you created!
	// You also have to destroy the pipelines
	void localCleanup() {
		DSLlocal.cleanup();
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) furnitureTypes[t]->model.cleanup();
		TSchool.cleanup();
		DSLglobal.cleanup();

		for (int i = 0; i < ICON_COUNT; i++) TIcons[i].cleanup();
		IconModel.cleanup();
		DSLIcon.cleanup();
		PIcon.destroy();
		RPIcon.destroy();

		P.destroy();

		RP.destroy();

		SC.localCleanup();
		txt.localCleanup();
	}




	// Wrapper callback used by the framework to access the member function
	static void populateCommandBufferAccess(VkCommandBuffer commandBuffer, int currentImage, void *Params) {
		// Recover the ClassroomPlanner instance passed through the generic pointer
		ClassroomPlanner *T = (ClassroomPlanner *)Params;
		T->populateCommandBuffer(commandBuffer, currentImage);
	}

	// Draw all preallocated slots of one furniture type
	//One furniture type shares one model, while each slot has its own descriptor set and is drawn with a separate draw call
	void drawFurnitureType(VkCommandBuffer commandBuffer, int currentImage, FurnitureTypeData &type) {
		type.model.bind(commandBuffer);
		for (size_t i = 0; i < type.DS.size(); i++) {
			type.DS[i].bind(commandBuffer, P, 1, currentImage);
			vkCmdDrawIndexed(commandBuffer,
				static_cast<uint32_t>(type.model.indices.size()), 1, 0, 0, 0);
		}
	}

	void populateCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
		// 1. Begin rendering the 3D scene into the current framebuffer
		RP.begin(commandBuffer, currentImage);
		// 2. Draw  scene
		SC.populateCommandBuffer(commandBuffer, 0, currentImage);

		// 3. Draw all furniture
		P.bind(commandBuffer);
		DSglobal.bind(commandBuffer, P, 0, currentImage);  // Bind global data to set 0

		// Bind each slots to set 1
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++)
			drawFurnitureType(commandBuffer, currentImage, *furnitureTypes[t]);

		RP.end(commandBuffer);
	}

	// The icon bar is a separate command buffer submitted independently
	static void populateIconCommandBufferAccess(VkCommandBuffer commandBuffer, int currentImage, void *Params) {
		ClassroomPlanner *T = (ClassroomPlanner *)Params;
		T->populateIconCommandBuffer(commandBuffer, currentImage);
	}

	void populateIconCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
		RPIcon.begin(commandBuffer, currentImage);

		// 2. Bind the icon pipeline and icon mesh
		PIcon.bind(commandBuffer);
		IconModel.bind(commandBuffer);

		// 3. Draw each icon
		for (int i = 0; i < ICON_COUNT; i++) {
			DSIcon[i].bind(commandBuffer, PIcon, 0, currentImage);
			vkCmdDrawIndexed(commandBuffer, 6, 1, i * 6, 0, 0);
		}

		RPIcon.end(commandBuffer);
	}

	// Build the layout and mesh of the icon bar.
	// This function is called during initialization and again after the window is resized.
	void buildIconMesh() {

	    // Define the icon size in NDC coordinates.
	    // Divide the width by the aspect ratio to keep the icons visually square.
	    iconHalfH = 0.075f;
	    iconHalfW = iconHalfH / Ar;
	    float spacing = iconHalfW * 2.6f;

	    // Decide the number of rows and columns.
	    // Use two rows when there are more than 9 icons.
	    int rows = (ICON_COUNT > 9) ? 2 : 1;
	    int cols = (ICON_COUNT + rows - 1) / rows;

	    // Limit the total width of each row.
	    // If the icons would exceed the screen width, scale them down proportionally.
	    const float MAX_ROW_WIDTH = 1.8f;
	    if (cols > 1) {
	        float neededWidth = spacing * (cols - 1);
	        if (neededWidth > MAX_ROW_WIDTH) {
	            float shrink = MAX_ROW_WIDTH / neededWidth;
	            iconHalfH *= shrink;
	            iconHalfW = iconHalfH / Ar;
	            spacing = iconHalfW * 2.6f;
	        }
	    }

	    // Define the vertical spacing between rows.
	    float rowSpacing = iconHalfH * 2.3f;

	    // Compute the center position of each icon in NDC coordinates.
	    for (int i = 0; i < ICON_COUNT; i++) {
	        int row = i / cols;
	        int col = i % cols;

	        // Compute how many icons are contained in the current row.
	        int firstIconOfRow = row * cols;
	        int iconsInThisRow =
	            (firstIconOfRow + cols <= ICON_COUNT)
	            ? cols
	            : (ICON_COUNT - firstIconOfRow);

	        // Center each row horizontally.
	        float rowStartX =
	            -spacing * (iconsInThisRow - 1) / 2.0f;

	        // In this Vulkan NDC convention, smaller Y values are closer to the top.
	        // Move each additional row downward by rowSpacing.
	        iconCenter[i] = glm::vec2(
	            rowStartX + col * spacing,
	            -1.0f + iconHalfH + 0.03f + row * rowSpacing
	        );
	    }

	    // Build one mesh containing all icon rectangles.
	    // Each icon uses 4 vertices and 6 indices (two triangles).
	    std::vector<unsigned char> newVertices(
	        ICON_COUNT * 4 * sizeof(IconVertex)
	    );
	    std::vector<uint32_t> newIndices(
	        ICON_COUNT * 6
	    );

	    IconVertex *V =
	        (IconVertex *)newVertices.data();

	    for (int i = 0; i < ICON_COUNT; i++) {
	        glm::vec2 c = iconCenter[i];

	        // Define the four corners of the icon and their UV coordinates.
	        V[i*4 + 0] = {
	            {c.x - iconHalfW, c.y - iconHalfH},
	            {0.0f, 0.0f}
	        }; // top-left

	        V[i*4 + 1] = {
	            {c.x + iconHalfW, c.y - iconHalfH},
	            {1.0f, 0.0f}
	        }; // top-right

	        V[i*4 + 2] = {
	            {c.x - iconHalfW, c.y + iconHalfH},
	            {0.0f, 1.0f}
	        }; // bottom-left

	        V[i*4 + 3] = {
	            {c.x + iconHalfW, c.y + iconHalfH},
	            {1.0f, 1.0f}
	        }; // bottom-right

	        // Build two triangles for each rectangular icon.
	        newIndices[i*6 + 0] = i*4 + 0;
	        newIndices[i*6 + 1] = i*4 + 1;
	        newIndices[i*6 + 2] = i*4 + 2;

	        newIndices[i*6 + 3] = i*4 + 1;
	        newIndices[i*6 + 4] = i*4 + 2;
	        newIndices[i*6 + 5] = i*4 + 3;
	    }

	    // If the mesh is being rebuilt after a resize,
	    // wait until the GPU has finished using the old buffers before destroying them.
	    if (IconModel.indices.size() > 0) {
	        vkDeviceWaitIdle(device);
	        IconModel.cleanup();
	    }

	    // Replace the old mesh data and upload the new mesh to the GPU.
	    IconModel.vertices = std::move(newVertices);
	    IconModel.indices  = std::move(newIndices);
	    IconModel.initMesh(this, &VDIcon, false);
	}

	// Here is where you update the uniforms.
	// Very likely this will be where you will be writing the logic of your application.
	void updateUniformBuffer(uint32_t currentImage) {

		// 1. Deal with the window resize
		// if the size of window change , calculate the Ar
		if (iconLayoutDirty) {
			buildIconMesh();
			resetCommandBuffers();
			iconLayoutDirty = false;
		}

		// 2. handle the ESC key to exit the app
		if(glfwGetKey(window, GLFW_KEY_ESCAPE)) {
			glfwSetWindowShouldClose(window, GL_TRUE);
		}

		// moves the view
		float deltaT = GameLogic();

		//3. Update the global light parameter
		static float lightRotationAngle = 0.0f;
		lightRotationAngle += -0.5f * deltaT;
		const glm::mat4 lightView = glm::rotate(glm::mat4(1), glm::radians(lightRotationAngle), glm::vec3(0.0f, 1.0f, 0.0f)) * 
									glm::rotate(glm::mat4(1), glm::radians(-45.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::vec3 lightDir =  glm::vec3(lightView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));

		GlobalUniformBufferObject gubo{};
		gubo.lightDir = lightDir;
		gubo.lightColor    = glm::vec3(1.0f, 1.0f, 1.0f);
		gubo.ambientLight  = glm::vec3(0.05f, 0.05f, 0.05f);
		gubo.eyePos = glm::vec3(glm::inverse(View)[3]);
		// transfer the paramter to shader
		DSglobal.map(currentImage, &gubo, 0);

		// 4. Calculate the parameters of those Scene objects in the scene.json file
		UniformBufferObject ubo{};
		int instanceId;
		for(instanceId = 0; instanceId < SC.TI[0].InstanceCount; instanceId++) {
			ubo.mMat = SC.TI[0].I[instanceId].Wm;
			ubo.mvpMat = ViewPrj * ubo.mMat;
			ubo.nMat    = glm::inverse(glm::transpose(ubo.mMat));
			ubo.specular = glm::vec4(1.0f, 1.0f, 1.0f, 150.0f);
			ubo.ambient  = glm::vec3(1.0f, 1.0f, 1.0f);
			ubo.highlight = glm::vec4(0.0f);

			// DS[1] = Pchar pass (main render): set0=DSLglobal, set1=DSLlocal
			SC.TI[0].I[instanceId].DS[0][0]->map(currentImage, &gubo, 0); // global (light/camera)
			SC.TI[0].I[instanceId].DS[0][1]->map(currentImage, &ubo, 0); // camera MVPs
		}

		//5. Calculate the parameters of the furniture in the Furniture System, prepare them for the shader
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++)
			updateFurnitureType(t, *furnitureTypes[t], currentImage);
		
		// 6.updates the FPS
		//FPS is calculated  to monitor the rendering performance of the application
		static float elapsedT = 0.0f;
		static int countedFrames = 0;
		
		countedFrames++;
		elapsedT += deltaT;
		if(elapsedT > 1.0f) {
			float Fps = (float)countedFrames / elapsedT;
			
			std::ostringstream oss;
			oss << "FPS: " << Fps << "\n";
			txt.print(1.0f, -1.0f, oss.str(), 1, "CO", false, false, true,TAL_RIGHT,TRH_RIGHT,TRV_TOP,{1.0f,0.0f,0.0f,1.0f},{0.8f,0.8f,0.0f,1.0f});

			elapsedT = 0.0f;
		    countedFrames = 0;
		}
		
		txt.updateCommandBuffer();
	}

	// Update all slots of one furniture type
	// Compute the transformation and rendering data for each instance and upload them to the corresponding Descriptor Set.
	void updateFurnitureType(int typeIndex, FurnitureTypeData &type, uint32_t currentImage) {
	    UniformBufferObject ubo{};

	    // 1. Create a pulsing highlight effect for the selected furniture.
	    float pulse = 0.30f + 0.15f * sinf((float)glfwGetTime() * 4.0f);

		//2. Iterate through all slots of the current furniture type
	    for (size_t i = 0; i < type.inst.size(); i++) {
	        FurnitureInstance &fi = type.inst[i];
	        glm::mat4 Wm;

	        if (fi.active) {
	            // Build the world matrix from position, user rotation
	            Wm = glm::translate(glm::mat4(1), fi.pos) *
	                 glm::rotate(glm::mat4(1), fi.rotY, glm::vec3(0, 1, 0)) *
	                 glm::scale(glm::mat4(1), glm::vec3(type.baseScale)) *
	                 type.baseRot;

	            // Compute the normal matrix used for lighting.
	            ubo.nMat = glm::inverse(glm::transpose(Wm));

	        } else {
	            // Inactive slots are scaled to zero so they are not visible.
	            Wm = glm::scale(glm::mat4(1), glm::vec3(0.0f));
	            ubo.nMat = glm::mat4(1);
	        }

	        // 3. Prepare the per-object transformation and material parameters.
	        ubo.mMat     = Wm;
	        ubo.mvpMat   = ViewPrj * Wm;
	        ubo.specular = glm::vec4(1.0f, 1.0f, 1.0f, 100.0f);
	        ubo.ambient  = glm::vec3(1.0f, 1.0f, 1.0f);

	        // 4. Check whether this instance is the currently selected furniture
	        bool isSelected =
	            fi.active &&
	            typeIndex == selectedType &&
	            (int)i == selectedIndex;
	        // Apply a yellow pulsing highlight only to the selected instance
	        ubo.highlight = isSelected
	            ? glm::vec4(1.0f, 0.85f, 0.15f, pulse)
	            : glm::vec4(0.0f);

	        // 5. Upload the per-instance UBO data to Descriptor Set for rendering
	        type.DS[i].map(currentImage, &ubo, 0);
	    }
	}





	// ----- Furniture interaction
	//convert mouse position into 3D ray
	bool computeMouseRay(glm::vec3 &origin, glm::vec3 &dir, const glm::mat4 &Proj, const glm::mat4 &View) {
		// 1. Get mouse position and window size
		double mx, my;
		glfwGetCursorPos(window, &mx, &my);
		int w, h;
		glfwGetWindowSize(window, &w, &h);
		if (w == 0 || h == 0) return false;

		// 2. Convert screen coordinates to NDC
		float ndcX = (float)(2.0 * mx / w - 1.0);
		float ndcY = (float)(2.0 * my / h - 1.0);

		// 3. Unproject near and far points into world space
		glm::mat4 invVP = glm::inverse(Proj * View);
		glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		glm::vec4 farH  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);//homogeneous coordinates
		glm::vec3 nearP = glm::vec3(nearH) / nearH.w;
		glm::vec3 farP  = glm::vec3(farH)  / farH.w;

		// 4. Build the 3D mouse ray
		origin = nearP;
		dir    = glm::normalize(farP - nearP);
		return true;
	}
	// Find the first available slot of one furniture type
	int findFreeSlot(FurnitureTypeData &type) {
		for (size_t i = 0; i < type.inst.size(); i++)
			if (!type.inst[i].active) return (int)i;
		return -1;
	}
    // Find the active furniture object whose center is closest to the mouse ray
	bool findNearestFurnitureByRay(const glm::vec3 &rayOrigin, const glm::vec3 &rayDir,
								   float maxDist, int &outType, int &outIndex) {
		float best = maxDist;
		bool found = false;

		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
			FurnitureTypeData *fd = furnitureTypes[t];
			for (size_t i = 0; i < fd->inst.size(); i++) {
				if (!fd->inst[i].active) continue;

				glm::vec3 toObj = fd->inst[i].pos - rayOrigin;
				//Project the vector to the object onto the ray direction
				float proj = glm::dot(toObj, rayDir);
				if (proj < 0.0f) continue;

				glm::vec3 closestOnRay = rayOrigin + proj * rayDir;
				float d = glm::distance(closestOnRay, fd->inst[i].pos);
				if (d < best) { best = d; outType = t; outIndex = (int)i; found = true; }
			}
		}
		return found;
	}





	// Save file path (in the current working directory
	const std::string SAVE_FILE_PATH = "saved_layout.json";


	void saveLayout() {
		// 1. Create the JSON structure
		nlohmann::json j;
		j["furniture"] = nlohmann::json::array();

		// 2. Collect all active furniture instances
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
			FurnitureTypeData *fd = furnitureTypes[t];
			for (size_t i = 0; i < fd->inst.size(); i++) {
				if (!fd->inst[i].active) continue;

				nlohmann::json item;
				item["type"] = FurnitureTypeNames[t];
				item["pos"]  = { fd->inst[i].pos.x, fd->inst[i].pos.y, fd->inst[i].pos.z };
				item["rotY"] = fd->inst[i].rotY;
				j["furniture"].push_back(item);
			}
		}
		// 3. Open the save file
		std::ofstream ofs(SAVE_FILE_PATH);
		if (!ofs.is_open()) {
			std::cout << "Failed to open " << SAVE_FILE_PATH << "\n";
			return;
		}

		// 4. Write the JSON data to the file
		ofs << j.dump(2);
		ofs.close();
		std::cout << "Layout saved to " << SAVE_FILE_PATH
			 << " (" << j["furniture"].size()
			 << " furniture objects)\n";
	}

	// Load a previously saved classroom layout
	void loadLayout() {
		// 1. Open the save file
		std::ifstream ifs(SAVE_FILE_PATH);
		if (!ifs.is_open()) {
			std::cout << "Save file not found: " << SAVE_FILE_PATH << "\n";
			return;
		}

		// 2. Read and parse the JSON file
		nlohmann::json j;
		try {
			ifs >> j;
		} catch (...) {
			std::cout << "Failed to read the save file: invalid JSON format.\n";
			return;
		}

		ifs.close();


		// 3. Clear the current furniture layout
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
			for (auto &inst : furnitureTypes[t]->inst) inst.active = false;
		}
		selectedType = selectedIndex = -1;

		// 4. Reconstruct all furniture objects from the JSON data
		int loaded = 0, skipped = 0;
		for (auto &item : j["furniture"]) {
			std::string typeName = item["type"];

			int type = -1;
			for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
				if (typeName == FurnitureTypeNames[t]) { type = t; break; }
			}
			if (type < 0) {
				std::cout << "Unknown furniture type: " << typeName << "\n";
				skipped++;
				continue;
			}

			FurnitureTypeData *fd = furnitureTypes[type];
			int slot = findFreeSlot(*fd);
			if (slot < 0) {
				std::cout << "No free slot available for " << typeName << "\n";
				skipped++;
				continue;
			}

			fd->inst[slot].active = true;
			fd->inst[slot].pos    = glm::vec3(item["pos"][0], item["pos"][1], item["pos"][2]);
			fd->inst[slot].rotY   = item["rotY"];
			loaded++;
		}

		// 5. Print the loading result
		std::cout << "Layout loaded from " << SAVE_FILE_PATH
				  << ": " << loaded << " loaded, "
				  << skipped << " skipped.\n";
	}

	// Spawn a new furniture object at a predefined default position
	void spawnFurniture(int type) {
		// 1. Default spawn positions
		static const glm::vec3 DEFAULT_SPAWN_POS[FURNITURE_TYPE_COUNT] = {
			glm::vec3(0.0f, 0.0f, -5.0f),   // DESK
			glm::vec3(-2.0f, 0.0f, -5.0f),   // CHAIR
			glm::vec3(0.0f, 1.0f, -11.8f),  // BLACKBOARD
			glm::vec3(-2.0f, 0.0f, -5.0f),   // SHELF
			glm::vec3(0.0f, 0.0f, -5.0f),   // PODIUM
			glm::vec3(0.0f, 0.0f, -5.0f),   // LOCKER
			glm::vec3(-4.0f, 0.0f, -5.0f),  // DESK2
			glm::vec3(-3.0f, 0.0f, -6.0f),  // CHAIR2
			glm::vec3(-2.0f, 0.0f, -5.0f),  // DESK3
			glm::vec3( 0.0f, 0.0f, -5.0f),  // CHAIR3
			glm::vec3( 3.0f, 0.0f, -6.0f),  // PLANT1
			glm::vec3( 3.0f, 0.0f, -6.0f),  // PLANT2
			glm::vec3( 6.0f, 0.0f, -6.0f),  // PAPER
			glm::vec3(-6.0f, 0.0f, -6.0f),  // CLOCK
			glm::vec3(-3.0f, 0.0f, -6.0f),  // NOTICEBOARD
			glm::vec3( 0.0f, 0.0f, -5.0f),  // RACK2
			glm::vec3( 3.0f, 0.0f, -6.0f),  // BOOK
			glm::vec3( 6.0f, 0.0f, -6.0f),  // LAPTOP
		};
		// 2. Get the selected furniture type
		FurnitureTypeData *fd = furnitureTypes[type];

		// 3. Find an available preallocated instance slot
		int slot = findFreeSlot(*fd);
		if (slot < 0) {
			std::cout << "No free slot available for this furniture type.\n";
			return;
		}

		// 4. Activate and initialize the new furniture instance
		fd->inst[slot].active = true;
		fd->inst[slot].pos    = DEFAULT_SPAWN_POS[type];
		fd->inst[slot].rotY   = 0.0f;
		selectedType  = type;
		selectedIndex = slot;
	}

	// furniture interaction every frame
	void updateFurnitureInteraction(float deltaT, const glm::vec3 &r, const glm::mat4 &Proj, const glm::mat4 &View) {

		// 1. Detect a single key press( only
		auto pressedOnce = [this](int key, bool &prevState) {
			bool now = glfwGetKey(window, key) == GLFW_PRESS;
			bool edge = now && !prevState;
			prevState = now;
			return edge;
		};

		static bool prev0 = false, prevDelete = false, prevFire = false;
		static bool prevSave = false, prevLoad = false;


		// 2. Handle selection and save/load shortcuts
		// Press 0 to clear the current selection.
		if (pressedOnce(GLFW_KEY_0, prev0)) { selectedType = selectedIndex = -1; }

		// F5 saves  F9 loads
		if (pressedOnce(GLFW_KEY_F5, prevSave)) { saveLayout(); }
		if (pressedOnce(GLFW_KEY_F9, prevLoad)) { loadLayout(); }


		// 3. Spawn furniture from the 2D icon bar
		// same as the detection function
		static bool prevLeftClick = false;
		bool leftNow  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		bool leftEdge = leftNow && !prevLeftClick;
		prevLeftClick = leftNow;

		//convert the mouse position to NDC, check whether it falls inside one of the icon rectangles
		if (leftEdge) {
			double mx, my;
			glfwGetCursorPos(window, &mx, &my);
			int w, h;
			glfwGetWindowSize(window, &w, &h);
			if (w > 0 && h > 0) {
				float ndcX = (float)(2.0 * mx / w - 1.0);
				float ndcY = (float)(2.0 * my / h - 1.0);
				for (int i = 0; i < ICON_COUNT; i++) {
					if (std::fabs(ndcX - iconCenter[i].x) <= iconHalfW &&
						std::fabs(ndcY - iconCenter[i].y) <= iconHalfH) {

						spawnFurniture(i);
						break;
					}
				}
			}
		}

		// 4. Select existing furniture with the right mouse button
		//same edge detection
		bool fireNow  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
		bool fireEdge = fireNow && !prevFire;
		prevFire = fireNow;
		// A single right-click is converted into a 3D mouse ray find the closest object
		if (fireEdge) {
			glm::vec3 rayOrigin, rayDir;
			if (computeMouseRay(rayOrigin, rayDir, Proj, View)) {
				int t, i;
				if (findNearestFurnitureByRay(rayOrigin, rayDir, 1.2f, t, i)) {
					selectedType  = t;
					selectedIndex = i;
				} else {
					selectedType = selectedIndex = -1;
				}
			}
		}

		// 5. Transform the currently selected furniture object
		if (selectedIndex >= 0) {
			FurnitureInstance &fi = furnitureTypes[selectedType]->inst[selectedIndex];
			const float MOVE_SPEED = 3.0f;
			const float ROT_SPEED  = glm::radians(100.0f);

			if (glfwGetKey(window, GLFW_KEY_I)) fi.pos.z -= MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_K)) fi.pos.z += MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_J)) fi.pos.x -= MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_L)) fi.pos.x += MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_U)) fi.pos.y += MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_O)) fi.pos.y -= MOVE_SPEED * deltaT;

			// 6. Apply room boundary constraints
			FurnitureTypeData *ft = furnitureTypes[selectedType];
			float marginX = ft->halfX + WALL_THICKNESS_MARGIN;
			float marginZ = ft->halfZ + WALL_THICKNESS_MARGIN;
			fi.pos.x = glm::clamp(fi.pos.x, ROOM_MIN_X + marginX, ROOM_MAX_X - marginX);
			fi.pos.y = glm::clamp(fi.pos.y, ROOM_MIN_Y, ROOM_MAX_Y);
			fi.pos.z = glm::clamp(fi.pos.z, ROOM_MIN_Z + marginZ, ROOM_MAX_Z - marginZ);

			fi.rotY += r.z * ROT_SPEED * deltaT;



			bool deleteNow = glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS ||
							  glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
			bool deleteEdge = deleteNow && !prevDelete;
			prevDelete = deleteNow;
			if (deleteEdge) {
				fi.active = false;
				selectedType = selectedIndex = -1;
			}
		}
	}

	float GameLogic() {
		// 1. Define the camera projection parameters
		const float FOVy = glm::radians(45.0f);
		const float nearPlane = 0.1f;
		const float farPlane = 100.f;

		// 2. Read the frame time and user input
		float deltaT;
		glm::vec3 m = glm::vec3(0.0f), r = glm::vec3(0.0f);
		bool fire = false;
		getSixAxis(deltaT, m, r, fire);

		// 3. Build the projection matrix
		glm::mat4 Prj = glm::perspective(FOVy, Ar, nearPlane, farPlane);
		Prj[1][1] *= -1;

		// 4. Update the camera target, rotation, and zoom from user input
		static glm::vec3 CamTarget = glm::vec3(0.0f, 0.0f, -4.5f);
		static float Yaw      = 0.0f;
		static float Pitch    = glm::radians(-35.0f);
		static float Distance = 20.0f;

		const float MOVE_SPEED = 4.0f;
		const float ROT_SPEED  = glm::radians(90.0f);
		const float ZOOM_SPEED = 8.0f;

		// WASD (m.x, m.z)：Rotate the movement direction according to the current Yaw angle
		glm::vec3 moveDir = glm::vec3(
			glm::rotate(glm::mat4(1), Yaw, glm::vec3(0, 1, 0)) * // consider the Yaw to ensure forward to the screen
			glm::vec4(m.x, 0.0f, m.z, 0.0f));
		CamTarget.x += moveDir.x * MOVE_SPEED * deltaT;
		CamTarget.z += moveDir.z * MOVE_SPEED * deltaT;

		// R/F (m.y)：the distance between CamTarget and camera
		Distance -= m.y * ZOOM_SPEED * deltaT;
		Distance = glm::clamp(Distance, 4.0f, 22.0f);

		// Drag mouse(r.x, r.y): rotate the view around the CamTarget
		Pitch -= r.x * ROT_SPEED * deltaT;
		Yaw   -= r.y * ROT_SPEED * deltaT;
		// limit the range of pitch
		Pitch = glm::clamp(Pitch, glm::radians(-75.0f), glm::radians(-15.0f));


		// 5. Compute the camera position and view matrix
		glm::vec3 forward = glm::vec3(
			glm::rotate(glm::mat4(1), Yaw, glm::vec3(0, 1, 0)) *
			glm::rotate(glm::mat4(1), Pitch, glm::vec3(1, 0, 0)) *
			glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));

		// Camera's position in world coordinate system
		glm::vec3 CamPos = CamTarget - forward * Distance;
		// View Matrix transforms world coordinates into camera coordinates
		View = glm::rotate   (glm::mat4(1), -Pitch, glm::vec3(1, 0, 0)) *
			   glm::rotate   (glm::mat4(1), -Yaw,   glm::vec3(0, 1, 0)) *
			   glm::translate(glm::mat4(1), -CamPos);

		// View-Projection
		ViewPrj = Prj * View;

		// Pass the current frame's camera matrices to the interaction system
		updateFurnitureInteraction(deltaT, r, Prj, View);

		return deltaT;
	}
};


// This is the main: probably you do not need to touch this!
int main() {
	ClassroomPlanner app;

    try {
        app.run(false);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}