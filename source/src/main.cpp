// THIS IS THE FILE YOU MUST START FROM!

// This has been adapted from the Vulkan tutorial
#include <sstream>
#include <cmath>

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
	alignas(16) glm::mat4 nMat;      // 法线矩阵
	alignas(16) glm::vec4 specular;  // xyz=高光颜色, w=shininess
	alignas(16) glm::vec3 ambient;   // 环境光系数
	alignas(16) glm::vec4 highlight; // xyz=高亮描边颜色, w=强度(0=不高亮, >0=选中态叠加这个颜色)
};

struct GlobalUniformBufferObject {
	alignas(16) glm::vec3 lightDir;
	alignas(16) glm::vec3 lightColor;
	alignas(16) glm::vec3 eyePos;
	alignas(16) glm::vec3 ambientLight;
};

struct Vertex {
	glm::vec3 pos;
	glm::vec3 norm;   // 新增！法线
	glm::vec2 UV;
};

// 图标栏用的顶点格式：跟 3D 场景的 Vertex 完全不是一回事——
// pos 直接就是屏幕 NDC 坐标（[-1,1]），不需要法线，也不需要经过任何相机矩阵变换
struct IconVertex {
	glm::vec2 pos;
	glm::vec2 uv;
};


// ── 家具系统的数据结构 ──────────────────────────────────────────────

// 家具类型：对应我们导入的 mgcg 模型。新增一种家具，第一步就是在 FURNITURE_TYPE_COUNT
// 前面插一个新名字——下标顺序要跟后面 iconFiles[]、DEFAULT_SPAWN_POS[] 数组的顺序对应
enum FurnitureType {
	DESK, CHAIR, BLACKBOARD, SHELF, PODIUM, LOCKER,
	// 这一批是新加的：第二/第三套桌椅、绿植、纸张、钟表、布告栏、第二种书架、书本、笔记本电脑
	DESK2, CHAIR2, DESK3, CHAIR3, PLANT1, PLANT2, PAPER, CLOCK, NOTICEBOARD, RACK2, BOOK, LAPTOP,
	FURNITURE_TYPE_COUNT
};

// 每种类型最多能放几个（预分配的 Vulkan 资源数量上限）
// 数字设小一点：占用的 Descriptor Set 和显存都和这个数字成正比
const int MAX_DESKS       = 12;
const int MAX_CHAIRS      = 12;
const int MAX_BLACKBOARDS = 5;
const int MAX_SHELVES     = 4;
const int MAX_PODIUMS     = 3;   // 讲台，教室通常只有一个
const int MAX_LOCKERS     = 10;   // 储物柜
const int MAX_DESK2S      = 20;   // 第二套课桌(ClassTable_02)
const int MAX_CHAIR2S     = 20;   // 第二套椅子(ClassChair02)
const int MAX_DESK3S      = 20;   // 第三套桌子(Desk03，风格不一样)
const int MAX_CHAIR3S     = 20;   // 配套的滑轮椅(RollingChair01)
const int MAX_PLANT1S     = 8;   // 绿植 A(Env_Pot_01)
const int MAX_PLANT2S     = 8;   // 绿植 B(Env_Pot_03)
const int MAX_PAPERS      = 6;   // 纸张
const int MAX_CLOCKS      = 5;   // 钟表
const int MAX_NOTICEBOARDS= 5;   // 布告栏
const int MAX_RACK2S      = 10;   // 第二种书架(Library_Rack02)
const int MAX_BOOKS       = 10;   // 书本
const int MAX_LAPTOPS     = 10;   // 笔记本电脑

// 所有类型的槽位数量加起来，一共要占多少 Descriptor Set / 显存。
// 之前这里在 DPSZs 那几行是手写死的数字 30——新增家具类型时很容易忘记同步改这个数字，
// 一旦忘了就会在运行时报 Descriptor Pool 不够用的错，还不容易一眼看出原因。
// 改成从 MAX_* 常量自动加总，新增类型只要把新的 MAX_XXX 常量加进这个式子就行，不会再漏改。
const int TOTAL_FURNITURE_SLOTS = MAX_DESKS + MAX_CHAIRS + MAX_BLACKBOARDS + MAX_SHELVES + MAX_PODIUMS + MAX_LOCKERS
	+ MAX_DESK2S + MAX_CHAIR2S + MAX_DESK3S + MAX_CHAIR3S + MAX_PLANT1S + MAX_PLANT2S
	+ MAX_PAPERS + MAX_CLOCKS + MAX_NOTICEBOARDS + MAX_RACK2S + MAX_BOOKS + MAX_LAPTOPS;

// 房间边界：数字是从 scene.json 里量出来的墙的真实位置——后墙在 z=-9，左右墙在 x=-6/x=6，
// 地板一路铺到 z=0（教室入口那面没有墙）。
// 注意这里不再手动减一个固定的"余量"了：之前固定减 0.3 的问题是，那 0.3 对瘦的椅子够用，
// 对宽的课桌不够用（课桌中心没出墙，但边缘早就穿出去了），所以桌子看起来"漏"出房间。
// 现在改成夹住"家具中心点 ± 这种家具自己的 halfX/halfZ"，每种家具、每个轴各留出刚好够自己用的余量。
const float ROOM_MIN_X = -7.5f, ROOM_MAX_X = 7.5f;
const float ROOM_MIN_Z = -12.0f, ROOM_MAX_Z = 0.0f;
const float ROOM_MIN_Y =  0.0f, ROOM_MAX_Y =  3.0f;  // Y 也设个上限，防止 U 一直按把家具顶出天花板之外
const float WALL_THICKNESS_MARGIN = 0.15f;  // 墙本身也有厚度，再留一点点，别让家具紧贴着墙皮


// 一个"槽位"里存的东西：这个位置的家具是否存在、放在哪、朝向哪
struct FurnitureInstance {
	bool  active = false;        // false = 这个槽位当前是空的，不绘制
	glm::vec3 pos = glm::vec3(0.0f);  // 世界坐标系下的位置
	float rotY = 0.0f;                // 绕 Y 轴的旋转角度（弧度）
};

// 把"一种家具类型"需要的所有东西打包在一起：
// 一个共享的 Model（网格数据），一组 DescriptorSet（每个槽位一个），
// 一组 FurnitureInstance（和 DescriptorSet 一一对应，记录每个槽位的状态）
struct FurnitureTypeData {
	Model model;
	std::vector<DescriptorSet> DS;
	std::vector<FurnitureInstance> inst;
	glm::mat4 baseRot = glm::mat4(1.0f);  // 模型本身朝向的修正旋转，弥补美术建模时坐标系不一致的问题

	// 这个类型的家具在地面上占多大——用来做边界限制时，不能只夹家具的中心点，
	// 不然家具的一半会穿出墙外（中心点在墙内，但物体本身比较宽）。
	// 用骨架自带的 Colliders.hpp 量出模型的包围盒（转正之后的），分别记 X/Z 两个方向的半宽。
	// 注意：这里没有用"能盖住旋转后包围盒的最小圆"那种做法——那种做法对黑板这种
	// 又宽又薄的家具会严重高估：黑板 X 方向很宽、Z 方向很薄，取对角线半径会把 Z 方向的
	// 余量也撑得跟 X 一样大，导致黑板怎么都够不到后墙。分开算 X/Z 更准确，
	// 代价是如果家具被转到 45° 这类斜角，包围盒的余量会稍微不够、贴墙时可能有一点点穿模，
	// 这个误差很小，比黑板顶不到墙这种明显的 bug 更能接受
	float halfX = 0.0f;
	float halfZ = 0.0f;

	// 这个类型整体的缩放系数——素材包里各种小道具(盆栽、书本、纸张这些)跟桌椅柜子不是
	// 按同一个物理尺寸做的，直接摆出来经常显得特别小。给每种类型留一个统一的缩放倍数，
	// 需要哪种类型看起来大一点/小一点，改这一个数就行，不用去改模型文件
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
	// ── 家具系统 ──────────────────────────────────────────────────────
	Texture TSchool;   // School 文件夹下所有家具共用的贴图
	FurnitureTypeData Desk, Chair, Blackboard, Shelf, Podium, Locker;
	FurnitureTypeData Desk2, Chair2, Desk3, Chair3, Plant1, Plant2, Paper, Clock, NoticeBoard, Rack2, Book, Laptop;
	// 按 FurnitureType 枚举值查表用的指针数组，方便写"不管哪种类型，逻辑都一样"的通用代码
	// （比如"找最近的家具"要挨个遍历 4 种类型，用数组下标循环比写 4 遍一样的代码干净）
	FurnitureTypeData* furnitureTypes[FURNITURE_TYPE_COUNT];

	// ── 家具交互状态 ──────────────────────────────────────────────────
	// 不再需要"放置模式"了：点图标/按数字键直接生成，右键/空格永远是"选中最近的家具"
	int selectedType  = -1; // 当前选中的家具属于哪个类型（配合 selectedIndex 一起定位）
	int selectedIndex = -1; // 当前选中的家具在该类型槽位数组里的下标，-1 = 没有选中任何东西

	// ── 图标栏 UI（Phase 7）──────────────────────────────────────────
	// 这是一套独立的、纯 2D 的渲染管线：自己的顶点格式、自己的 DescriptorSetLayout、
	// 自己的 RenderPass、自己的 Pipeline，跟 3D 场景（P/RP/VD）完全不共用。
	// 写法照抄骨架自带的 TextMaker（画 FPS 文字那套）：RenderPass 用 LOAD 不 Clear，
	// 这样它画出来的东西会叠加在已经画好的 3D 场景上面，而不是把画面清空重画。
	static const int ICON_COUNT = FURNITURE_TYPE_COUNT; // 4 个图标，正好对应 4 种家具类型
	VertexDescriptor VDIcon;
	DescriptorSetLayout DSLIcon;
	RenderPass RPIcon;
	Pipeline PIcon;
	// 每种家具一张贴图、一个 DescriptorSet——用的是素材包自带的缩略图 PNG（跟 .mgcg 同名），
	// 不再是我们自己拼的图，所以不需要 UV 分格子，每个图标直接采样自己那张图的 (0,0)~(1,1)
	Texture TIcons[ICON_COUNT];
	DescriptorSet DSIcon[ICON_COUNT];
	Model IconModel;
	// 每个图标中心点的屏幕 NDC 坐标 + 半宽半高，建 mesh 和判断点击用的是同一份数据，
	// 保证"画出来的位置"和"点击判定的位置"永远一致
	glm::vec2 iconCenter[ICON_COUNT];
	float iconHalfW, iconHalfH;
	// 窗口大小变化后，图标是按旧的宽高比算的，会跑位，需要重新算一遍布局、重新上传顶点数据。
	// resize 回调可能一瞬间触发好几次，不在回调里直接动 GPU 资源，而是打个"需要重建"的标记，
	// 等下一次 updateUniformBuffer 时统一处理（这个做法参考自更早的 WardPlanner 项目）
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
		Ar = (float)w / (float)h;
		// Update Render Pass
		RP.width = w;
		RP.height = h;
		RPIcon.width = w;
		RPIcon.height = h;
		// 图标布局是按 Ar（宽高比）算的，Ar 已经在上面更新了，
		// 这里只打个标记，真正重建顶点数据放到 updateUniformBuffer 里做（见那边的注释）
		iconLayoutDirty = true;

		// updates the textual output
		txt.resizeScreen(w, h);
	}
	
	// Here you load and setup all your Vulkan Models and Texutures.
	// Here you also create your Descriptor set layouts and load the shaders for the pipelines
	void localInit() {
		// Descriptor Layouts [what will be passed to the shaders]
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

		// ── 加载家具的 Model 和共享 Texture ──────────────────────────────
		// 新增一种家具类型，第二步就是在这里加一行 .model.init(...)，
		// 路径填 School 素材包里对应的 .mgcg 文件
		Desk.model.init(this, &VD, "assets/models/School/SM_School_ComputerClass_Desk01.mgcg", MGCG);
		Chair.model.init(this, &VD, "assets/models/School/SM_School_ComputerClass_Chair01.mgcg", MGCG);
		Blackboard.model.init(this, &VD, "assets/models/School/SM_School_BlackBoard.mgcg", MGCG);
		Shelf.model.init(this, &VD, "assets/models/School/SM_School_Library_Rack01.mgcg", MGCG);
		Podium.model.init(this, &VD, "assets/models/School/SM_School_Props_Podium.mgcg", MGCG);
		Locker.model.init(this, &VD, "assets/models/School/SM_School_Props_Locker.mgcg", MGCG);

		// 新加的一批：第二/第三套桌椅、绿植、纸张、钟表、布告栏、第二种书架、书本、笔记本电脑
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

		// School 素材包所有模型共用同一张贴图，所以新增家具类型不需要再多加载一张 Texture，
		// 只要模型本身也是这批 School 资产就行
		TSchool.init(this, "assets/textures/School/Tex_School.png");






		// 预分配每种类型固定数量的槽位（DescriptorSet 和 实例数据 一一对应）。
		// 第三步：新类型要在这里用它自己的 MAX_XXX 常量 resize 一下
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


		// 建好查表用的指针数组，下标就是 FurnitureType 枚举值。
		// 第四步：把新类型的地址按枚举下标填进这个数组——填完这一步之后，
		// baseRot 修正、halfX/halfZ 测量、边界限制、选中高亮这些逻辑全都是循环着
		// 遍历这个数组写的，不需要再为新类型额外改这些地方
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

		// School 这批家具素材全都是同一个人建模的，统一习惯是"躺平"建模
		// （模型的"高度"方向其实是它本地坐标系的 Z 轴，不是 Y 轴），
		// 所以每一种家具都需要绕 X 轴转 90 度才能立起来——这不是黑板/书架/椅子
		// 各自独立的问题，是整批资产的通用规律。
		// 用 furnitureTypes[] 这个已有的查表数组写一个循环来统一设置，
		// 而不是给每个类型手写一行 `XXX.baseRot = ...`：
		// 好处是以后新增第 5 种家具时，只要遵循同一套建模习惯，
		// 把它加进 furnitureTypes[] 数组，这里的循环会自动把修正应用上去，
		// 不需要再手动补一行调用。
		// 注意：这批新加的小道具(绿植/纸张/钟表/布告栏/书本/笔记本电脑)体型都比较小、比较简单，
		// 不能保证美术做的时候也是照着"桌椅墙"那一批的躺平习惯来建模的——运行起来之后要
		// 挨个看一眼朝向，如果哪个东西立起来是歪的，就把它单独从这个循环里摘出来，
		// 在这行下面单独写一句 `Paper.baseRot = ...`(或者干脆设回 glm::mat4(1) 表示不用修正)，
		// 而不是继续跟其余类型共用 standUpFix
		glm::mat4 standUpFix = glm::rotate(glm::mat4(1), glm::radians(90.0f), glm::vec3(1, 0, 0));
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
			furnitureTypes[t]->baseRot = standUpFix;
		}

		// 盆栽这批小道具素材本身做得比较小，摆出来跟桌椅柜子比例不协调，单独给它们放大一点。
		// 想放更大/更小，直接改这两个数字就行
		Plant1.baseScale = 2.2f;
		Plant2.baseScale = 2.2f;

		// ── 每种家具占地多大：用骨架自带的 Colliders.hpp 量出来 ──────────
		// fitAABB 直接读 Model 的顶点数据算出包围盒（跟之前手动读字节数组算 AABB
		// 是同一件事，只是这次用现成的框架工具，不用自己重新写一遍）。
		// setWorldMatrix(baseRot) 是为了把"躺平修正"这个旋转也考虑进去——
		// 不然量出来的包围盒会是模型没转正之前的形状，X/Z 会算错。
		// X、Z 方向的半宽分开记录（不合成一个圆的半径），原因见 FurnitureTypeData 里的注释。
		// 这里还要乘上 baseScale——包围盒是从原始模型数据量出来的，不知道后面会被放大/缩小，
		// 不乘的话，放大过的盆栽实际显示会比算出来的 halfX/halfZ 大一圈，边界限制会不够，
		// 靠墙的时候会穿模
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
			FurnitureTypeData *fd = furnitureTypes[t];
			Collider c;
			c.fitAABB(&fd->model);
			c.setWorldMatrix(fd->baseRot);
			AABBextents E = c.getExtents();
			fd->halfX = (E.xMax - E.xMin) / 2.0f * fd->baseScale;
			fd->halfZ = (E.zMax - E.zMin) / 2.0f * fd->baseScale;
		}

		// ── 默认布局：教室不应该一开始是空的，先摆一套基础桌椅当起点 ──
		// 2 排 × 3 列 = 6 套桌椅，面朝黑板（黑板贴在 z=-12 那面后墙上）。
		// 这些坐标是配合 scene.json 里现在这个 15(宽,x:-7.5~7.5) × 12(深,z:-12~0) 的房间量的，
		// 房间大小改了这里就要跟着改，不然家具会跟墙对不上（这次黑板离墙太远、桌椅偏前，就是
		// 房间从旧的 12×9 改成 15×12 之后，这些坐标忘了同步）。
		// 这些默认家具和后面手动放置的家具用的是同一套槽位数组，
		// 所以一样可以被鼠标点中、用 IJKL/Q/E/X 移动、旋转、删除
		{
			const float rowZ[2] = { -9.5f, -6.5f };
			const float colX[3] = { -4.5f, 0.0f, 4.5f };
			int slot = 0;
			for (int row = 0; row < 2; row++) {
				for (int col = 0; col < 3; col++) {
					Desk.inst[slot].active = true;
					Desk.inst[slot].pos    = glm::vec3(colX[col], 0.0f, rowZ[row]);

					Chair.inst[slot].active = true;
					Chair.inst[slot].pos    = glm::vec3(colX[col] + 1.3f, 0.0f, rowZ[row]);

					slot++;
				}
			}
		}
		Blackboard.inst[0].active = true;
		Blackboard.inst[0].pos    = glm::vec3(0.0f, 1.0f, -11.8f); // 贴着后墙(z=-12)

		Shelf.inst[0].active = true;
		Shelf.inst[0].pos    = glm::vec3(-6.8f, 0.0f, -2.0f);     // 靠左墙(x=-7.5)

		// 新家具的默认位置就是这样控制的：给它自己 inst[] 数组里的某个槽位下标（这里用 0，
		// 因为讲台/柜子暂时只摆一个）设 active=true 和一个 pos（世界坐标系下的 x,y,z）。
		// 这一步是可选的——如果不想让新家具默认就出现在房间里，跳过这几行不写就行，
		// 用户一样可以在运行时点图标手动生成
		Podium.inst[0].active = true;
		Podium.inst[0].pos    = glm::vec3(2.8f, 0.0f, -10.5f);    // 黑板右前方，讲台该在的地方

		Locker.inst[0].active = true;
		Locker.inst[0].pos    = glm::vec3(6.8f, 0.0f, -2.0f);     // 靠右墙(x=7.5)，跟书架对称摆在另一侧

		// initializes the render passes
		RP.init(this);
		// sets the blue sky
		RP.properties[0].clearValue = {0.0f,0.9f,1.0f,1.0f};

		// Pipelines [Shader couples]
		// The last array, is a vector of pointer to the layouts of the sets that will
		// be used in this pipeline. The first element will be set 0, and so on..
		
		P.init(this, &VD, "shaders/PosNormUV.vert.spv",
			  "shaders/LambertBlinn.frag.spv",
			  {&DSLglobal, &DSLlocal});


		// ── 图标栏 UI：独立的 2D 渲染管线（写法照抄 TextMaker）──────────────
		// 顶点格式：屏幕 NDC 坐标 + 贴图 UV，不需要法线（不参与光照）
		VDIcon.init(this, {
						{0, sizeof(IconVertex), VK_VERTEX_INPUT_RATE_VERTEX}
					}, {
						{0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(IconVertex, pos),
							   sizeof(glm::vec2), OTHER},
						{0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(IconVertex, uv),
							   sizeof(glm::vec2), UV}
					});
		// 只有一个"贴图采样器"绑定，没有 uniform buffer——图标的位置是直接烘焙进顶点数据里的，
		// 不需要像 3D 物体那样每帧传一个 MVP 矩阵
		DSLIcon.init(this, {
						{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 1}
					});

		// 素材包里每个 .mgcg 模型都配了一张同名的缩略图 PNG（School 文件夹里，256x256 带透明底），
		// 直接拿来当图标用，顺序要和 FurnitureType 枚举（DESK/CHAIR/BLACKBOARD/SHELF/PODIUM/LOCKER）对应。
		// 第五步：新类型在这个数组里按枚举顺序加一行，图标栏会自动多出一个可点的图标
		// （ICON_COUNT = FURNITURE_TYPE_COUNT，图标数量本身不用改）
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

		// 图标栏自己的 RenderPass：LOAD 而不是 CLEAR，这样它会叠加画在已经渲染好的
		// 3D 场景上面，不会把前面画的东西清掉（跟 TextMaker 画 FPS 文字的做法一模一样）
		// 传 -1,-1 而不是 windowWidth/windowHeight：RenderPass::init 内部看到 <=0 就会
		// 改用 BP->swapChainExtent 的真实尺寸。macOS 上实际窗口/framebuffer 大小经常和
		// setWindowParameters() 里请求的 800x600 对不上（视网膜屏缩放等原因），
		// 用请求值会导致刚启动时 RenderPass 的尺寸跟真实 surface 不一致（图标栏一开始很小、
		// 还会报 RenderPassBeginInfo 的验证层错误），直到发生一次真正的 resize 才会被纠正过来
		RPIcon.init(this, -1, -1, -1,
					RenderPass::getStandardAttchmentsProperties(AT_SURFACE_NOAA_DEPTH, this));
		RPIcon.properties[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		RPIcon.properties[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		RPIcon.properties[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		RPIcon.properties[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		PIcon.init(this, &VDIcon, "shaders/Icon.vert.spv", "shaders/Icon.frag.spv", {&DSLIcon});
		PIcon.setCullMode(VK_CULL_MODE_NONE);
		PIcon.setTransparency(true); // 贴图带透明背景，需要开混合才能看到镂空效果
		// 跟 TextMaker 画文字用的设置一样：允许"深度相等"也画进去，
		// 保证图标栏稳稳地叠在 3D 场景最上面
		PIcon.setCompareOp(VK_COMPARE_OP_LESS_OR_EQUAL);

		// Ar 目前还是 setWindowParameters() 里写死的 4/3，但窗口真实的 framebuffer 比例
		// 启动时就可能跟这个不一样（同样是前面说的 macOS 缩放问题），这里用真实尺寸纠正一下，
		// 这样图标栏从第一帧开始就是按正确的宽高比算的，不用等一次 resize 才修好
		{
			int fbW, fbH;
			glfwGetFramebufferSize(window, &fbW, &fbH);
			if (fbW > 0 && fbH > 0) Ar = (float)fbW / (float)fbH;
		}

		// 布局 + 建 mesh 这部分逻辑会在窗口大小变化时重复用到，所以抽成了一个单独的函数（见下面 buildIconMesh）
		buildIconMesh();

		// sets the size of the Descriptor Set Pool (it MUST be done before loading the scene)
		// 这里原来是手写死的数字 30，跟当时 4 种家具的 MAX_* 常量加起来正好相等，
		// 但这只是巧合式地"当时对"——新增家具类型、忘记同步改这个数字，就会在运行时
		// 报 Descriptor Pool 用满的错，还很难第一时间联想到是这里漏改了。
		// 换成 TOTAL_FURNITURE_SLOTS（在最上面按 MAX_DESKS+MAX_CHAIRS+... 自动加总）之后，
		// 新增第七种家具时只要把它的 MAX_XXX 加进那个加总式子，这里完全不用碰
		DPSZs.uniformBlocksInPool = 2  + TOTAL_FURNITURE_SLOTS;               // 原有(全局+地板) + 家具槽位数
		DPSZs.texturesInPool      = 1  + TOTAL_FURNITURE_SLOTS + ICON_COUNT;  // 原有(地板贴图) + 家具槽位数 + 每个图标各一张贴图
		DPSZs.setsInPool          = 2  + TOTAL_FURNITURE_SLOTS + ICON_COUNT;  // 原有 + 家具槽位数 + 每个图标各一个 DescriptorSet

		// to support scene
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

		// initializes the textual output
		// 这里不能直接传 windowWidth/windowHeight——那两个是 setWindowParameters() 里
		// 写死的"请求尺寸"（800x600），跟 macOS 上真实的 framebuffer 尺寸经常对不上
		// （Retina 缩放等原因，前面图标栏的 RPIcon 也踩过同一个坑）。
		// TextMaker 内部的 pixelToScr() 是拿 screenW/screenH 换算屏幕坐标的，
		// 如果这两个数从一开始就是错的，之后不管把文字放在 (1,1) 还是别的哪个角，
		// 换算出来的位置都会跟真实屏幕对不上——这正是之前文字没出现在右下角、
		// 反而跑到屏幕中间靠左的原因。用真实 framebuffer 尺寸初始化就对了。
		{
			int fbW, fbH;
			glfwGetFramebufferSize(window, &fbW, &fbH);
			txt.init(this, fbW, fbH);
		}

		// submits the main command buffer
		submitCommandBuffer("main", 0, populateCommandBufferAccess, this);

		// 图标栏的内容从来不会变（4 个图标，位置固定），所以只需要提交一次，
		// 不像 TextMaker 那样每次文字变了都要重新生成、重新提交
		submitCommandBuffer("iconbar", 5000, populateIconCommandBufferAccess, this);

		// Prepares for showing the FPS count.
		// FPS = 每秒渲染帧数，是最基本的性能计数器：数字越高说明画面越流畅（60 左右就很顺滑了），
		// 数字骤降通常意味着某处（比如某帧的 CPU 逻辑或 GPU 绘制）变慢了，调试性能问题时用得上，
		// 不是给最终用户看的功能，纯粹是我们自己排查渲染卡顿用的。
		// 挪到右上角（跟原来右下角对调），和右下角的操作说明分开，不会挤在一起
		txt.print(1.0f, -1.0f, "FPS:",1,"CO",false,false,true,TAL_RIGHT,TRH_RIGHT,TRV_TOP,{1.0f,0.0f,0.0f,1.0f},{0.8f,0.8f,0.0f,1.0f});

		// 操作说明：内容固定不变，所以只在 localInit 里 print 一次就够了，
		// 不用像 FPS 那样每帧/每秒重新 print——TextMaker 内部按 id 存这些文字块（见上面 Blocks[id]），
		// 同一个 id 只要没再调用 print 更新它，就会一直待在那不用管。
		// 注意：字体只支持 ASCII 字符（这是骨架自带的位图字体，字符表里没有中文），
		// 之前用中文写的时候，每个中文字符会被 measureText/createTextMesh 里的
		// "c>=0 && c<=fnt.maxChar" 检查直接跳过（不占宽度、不生成顶点），
		// 所以画出来是一堆断断续续的空格——这里改成纯英文就不会再丢字了。
		// 位置换成右下角：x=1,y=1 是屏幕最右下角（和 FPS 同一个角），
		// 所以这块文字往上挪一点（y=0.55），避免和 FPS 那行重叠
		// 第一行说清楚这是"相机"的操作，第三行说清楚是"被选中物体"的操作，
		// 之前这两组混在一起没有区分，容易让人以为 IJKL 是在控制镜头
		txt.print(1.0f, 0.55f,
			"Camera: WASD move / Arrows or drag rotate / R,F zoom\n"
			"Click icon to spawn / Right-click to select\n"
			"Selected object: IJKL move / U,O up-down / Q,E rotate / Del delete",
			2, "CO", false, false, true, TAL_RIGHT, TRH_RIGHT, TRV_TOP,
			{1.0f,1.0f,1.0f,1.0f}, {0.0f,0.0f,0.0f,1.0f});

	}
	
	// Here you create your pipelines and Descriptor Sets!
	void pipelinesAndDescriptorSetsInit() {
		// creates the render passes
		RP.create();
		
		// This creates a new pipeline (with the current surface), using its shaders for the provided render pass
		P.create(&RP);
		
		DSglobal.init(this, &DSLglobal, {});

		// 为每个家具槽位创建它的 Descriptor Set（贴图都用同一个 TSchool）。
		// 之前这里是 Desk/Chair/Blackboard/Shelf 四行几乎一样的代码，新增家具类型就要再手写一行，
		// 改成循环 furnitureTypes[] 之后，新类型只要在 localInit 里注册进了这个数组，
		// 这里、下面 cleanup、localCleanup 都不用再改
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++)
			for (auto &ds : furnitureTypes[t]->DS)
				ds.init(this, &DSLlocal, {TSchool.getViewAndSampler()});

		// 图标栏：每个图标一张贴图、一个 DescriptorSet
		RPIcon.create();
		PIcon.create(&RPIcon);
		for (int i = 0; i < ICON_COUNT; i++)
			DSIcon[i].init(this, &DSLIcon, {TIcons[i].getViewAndSampler()});

		// Here you define the data set
		// If the scene has textures coming from a render pass, the corresponding element of the technique must be
		// updated before calling SC.pipelinesAndDescriptorSetsInit();

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
	
	// Here it is the creation of the command buffer:
	// You send to the GPU all the objects you want to draw,
	// with their buffers and textures
	static void populateCommandBufferAccess(VkCommandBuffer commandBuffer, int currentImage, void *Params) {
		// Simple trick to avoid having always 'T->'
		// in che code that populates the command buffer!
		ClassroomPlanner *T = (ClassroomPlanner *)Params;
		T->populateCommandBuffer(commandBuffer, currentImage);
	}

	// 绑定某种家具类型的 Model,然后把它的每个槽位画一遍
	void drawFurnitureType(VkCommandBuffer commandBuffer, int currentImage, FurnitureTypeData &type) {
		type.model.bind(commandBuffer);
		for (size_t i = 0; i < type.DS.size(); i++) {
			type.DS[i].bind(commandBuffer, P, 1, currentImage);
			vkCmdDrawIndexed(commandBuffer,
				static_cast<uint32_t>(type.model.indices.size()), 1, 0, 0, 0);
		}
	}

	void populateCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {

		RP.begin(commandBuffer, currentImage);

		SC.populateCommandBuffer(commandBuffer, 0, currentImage);

		// ── 绘制家具 ──────────────────────────────────────────────
		P.bind(commandBuffer);
		DSglobal.bind(commandBuffer, P, 0, currentImage);  // 全局数据（光照/相机），绑到 set 0

		// 循环遍历 furnitureTypes[]，而不是给每种类型手写一行——新增类型不用再改这里
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++)
			drawFurnitureType(commandBuffer, currentImage, *furnitureTypes[t]);

		RP.end(commandBuffer);
	}

	// 图标栏是单独提交的一条命令缓冲区（跟 3D 场景那条完全分开），
	// 用的是自己的 RenderPass（RPIcon，LOAD 模式，不清屏），画在 3D 场景已经渲染好的画面上面
	static void populateIconCommandBufferAccess(VkCommandBuffer commandBuffer, int currentImage, void *Params) {
		ClassroomPlanner *T = (ClassroomPlanner *)Params;
		T->populateIconCommandBuffer(commandBuffer, currentImage);
	}

	void populateIconCommandBuffer(VkCommandBuffer commandBuffer, int currentImage) {
		RPIcon.begin(commandBuffer, currentImage);

		PIcon.bind(commandBuffer);
		IconModel.bind(commandBuffer);
		// 每个图标是自己的一张贴图，所以要挨个绑定各自的 DescriptorSet 分开画，
		// 每个图标在 IconModel 里占 6 个 index（第 i 个图标从 i*6 开始）
		for (int i = 0; i < ICON_COUNT; i++) {
			DSIcon[i].bind(commandBuffer, PIcon, 0, currentImage);
			vkCmdDrawIndexed(commandBuffer, 6, 1, i * 6, 0, 0);
		}

		RPIcon.end(commandBuffer);
	}

	// 图标栏的布局 + 顶点数据构建：初始化时调用一次，窗口 resize 之后还会再调用一次
	// （所以单独抽成函数，而不是写死在 localInit 里）
	void buildIconMesh() {
		// 布局：ICON_COUNT 个图标沿屏幕顶部摆放。
		// iconHalfW 除以 Ar 是为了让图标在屏幕上看起来是正方形而不是被拉伸的长方形
		// （NDC 的 x、y 分别对应窗口的宽、高，宽高比不是 1:1 时要用 Ar 修正一下）
		iconHalfH = 0.075f;
		iconHalfW = iconHalfH / Ar;
		float spacing = iconHalfW * 2.6f;

		// 图标一多，摆一整排会显得很挤/很小，超过 9 个就自动换成两排。
		// cols 用"向上取整"算，保证两排数量尽量平均（比如 18 个正好 9+9，两排对称；
		// 如果以后加到比如 19 个，会是 10+9，两排也只差一个，不会一排特别多一排特别少）
		int rows = (ICON_COUNT > 9) ? 2 : 1;
		int cols = (ICON_COUNT + rows - 1) / rows;

		// 每排够不够宽会超出屏幕左右两边——NDC 宽度总共只有 2(从 -1 到 1)，图标一多，
		// 乘上间距很容易超过这个数，超出去的部分会直接被裁到屏幕外面看不见。
		// 这里按"每排的图标数"(而不是总数)算需要多宽，超过阈值就把图标等比缩小
		const float MAX_ROW_WIDTH = 1.8f; // 留一点左右边距，不贴到屏幕最边缘
		if (cols > 1) {
			float neededWidth = spacing * (cols - 1);
			if (neededWidth > MAX_ROW_WIDTH) {
				float shrink = MAX_ROW_WIDTH / neededWidth;
				iconHalfH *= shrink;
				iconHalfW = iconHalfH / Ar;
				spacing = iconHalfW * 2.6f;
			}
		}

		// 两排之间留的垂直间距，跟左右间距用的是同一套比例(2.3 倍半高)
		float rowSpacing = iconHalfH * 2.3f;
		for (int i = 0; i < ICON_COUNT; i++) {
			int row = i / cols;
			int col = i % cols;
			// 最后一排如果没坐满(总数不是 cols 的整数倍)，让那一排单独居中，
			// 而不是贴着左边对齐——两排图标看起来才对称
			int firstIconOfRow = row * cols;
			int iconsInThisRow = (firstIconOfRow + cols <= ICON_COUNT) ? cols : (ICON_COUNT - firstIconOfRow);
			float rowStartX = -spacing * (iconsInThisRow - 1) / 2.0f;
			// y 越小越靠上（Vulkan NDC 是 Y-down），加一点边距贴着屏幕顶部，往下一排就加一个 rowSpacing
			iconCenter[i] = glm::vec2(rowStartX + col * spacing, -1.0f + iconHalfH + 0.03f + row * rowSpacing);
		}

		// 手工搭一个"由 ICON_COUNT 个矩形拼成"的 Model，跟 TextMaker 画文字用的是同一个手法：
		// 直接往 Model::vertices / indices 里填数据，再调用 initMesh 让它生成 GPU 缓冲区。
		// 每个图标现在是独立的一整张贴图，UV 就是普通的 0~1，不用再切格子。
		std::vector<unsigned char> newVertices(ICON_COUNT * 4 * sizeof(IconVertex));
		std::vector<uint32_t> newIndices(ICON_COUNT * 6);
		IconVertex *V = (IconVertex *)newVertices.data();
		for (int i = 0; i < ICON_COUNT; i++) {
			glm::vec2 c = iconCenter[i];

			V[i*4 + 0] = { {c.x - iconHalfW, c.y - iconHalfH}, {0.0f, 0.0f} }; // 左上
			V[i*4 + 1] = { {c.x + iconHalfW, c.y - iconHalfH}, {1.0f, 0.0f} }; // 右上
			V[i*4 + 2] = { {c.x - iconHalfW, c.y + iconHalfH}, {0.0f, 1.0f} }; // 左下
			V[i*4 + 3] = { {c.x + iconHalfW, c.y + iconHalfH}, {1.0f, 1.0f} }; // 右下

			newIndices[i*6 + 0] = i*4 + 0;
			newIndices[i*6 + 1] = i*4 + 1;
			newIndices[i*6 + 2] = i*4 + 2;
			newIndices[i*6 + 3] = i*4 + 1;
			newIndices[i*6 + 4] = i*4 + 2;
			newIndices[i*6 + 5] = i*4 + 3;
		}

		// 如果是重建（窗口 resize 之后），要先等 GPU 把当前用着这份缓冲区的帧都画完，
		// 再销毁旧的、传新的进去，不然可能会在还在用的显存上动手，导致崩溃或画面错乱
		if (IconModel.indices.size() > 0) {
			vkDeviceWaitIdle(device);
			IconModel.cleanup();
		}
		IconModel.vertices = std::move(newVertices);
		IconModel.indices  = std::move(newIndices);
		IconModel.initMesh(this, &VDIcon, false);
	}

	// Here is where you update the uniforms.
	// Very likely this will be where you will be writing the logic of your application.
	void updateUniformBuffer(uint32_t currentImage) {
		static bool debounce = false;
		static int curDebounce = 0;

		// 窗口大小变了：重新按新的宽高比算图标位置、重新传一份顶点数据上去。
		// 因为 IconModel 换了新的 VkBuffer，之前已经录好的命令缓冲区里绑的还是旧 buffer 的句柄，
		// 所以必须用 resetCommandBuffers() 让所有命令缓冲区（包括图标栏那条）重新录一遍
		if (iconLayoutDirty) {
			buildIconMesh();
			resetCommandBuffers();
			iconLayoutDirty = false;
		}

		// handle the ESC key to exit the app
		if(glfwGetKey(window, GLFW_KEY_ESCAPE)) {
			glfwSetWindowShouldClose(window, GL_TRUE);
		}

		// moves the view
		float deltaT = GameLogic();
		
		// defines the global parameters for the uniform
		static float lightRotationAngle = 0.0f; // Static variable to keep track of rotation
		lightRotationAngle += -0.5f * deltaT; // Increment rotation angle based on time

		const glm::mat4 lightView = glm::rotate(glm::mat4(1), glm::radians(lightRotationAngle), glm::vec3(0.0f, 1.0f, 0.0f)) * 
									glm::rotate(glm::mat4(1), glm::radians(-45.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		const glm::vec3 lightDir =  glm::vec3(lightView * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));

		GlobalUniformBufferObject gubo{};

		gubo.lightDir = lightDir;
		gubo.lightColor    = glm::vec3(1.0f, 1.0f, 1.0f);
		gubo.ambientLight  = glm::vec3(0.05f, 0.05f, 0.05f);
		gubo.eyePos = glm::vec3(glm::inverse(View)[3]);

		DSglobal.map(currentImage, &gubo, 0);

		// defines the local parameters for the uniforms
		UniformBufferObject ubo{};		

		int instanceId;
		// character
		for(instanceId = 0; instanceId < SC.TI[0].InstanceCount; instanceId++) {
			ubo.mMat = SC.TI[0].I[instanceId].Wm;
			ubo.mvpMat = ViewPrj * ubo.mMat;
			ubo.nMat    = glm::inverse(glm::transpose(ubo.mMat));
			ubo.specular = glm::vec4(1.0f, 1.0f, 1.0f, 150.0f); // 白色高光，shininess=150
			ubo.ambient  = glm::vec3(1.0f, 1.0f, 1.0f);           // 白色环境系数
			ubo.highlight = glm::vec4(0.0f);                      // 场景角色不参与选中系统，永远不高亮

			// DS[1] = Pchar pass (main render): set0=DSLglobal, set1=DSLlocal
			SC.TI[0].I[instanceId].DS[0][0]->map(currentImage, &gubo, 0); // global (light/camera)
			SC.TI[0].I[instanceId].DS[0][1]->map(currentImage, &ubo, 0); // camera MVPs
		}

		// 更新家具的世界矩阵（和地板的实例循环无关，只需要跑一次）
		// 循环遍历 furnitureTypes[]，而不是给每种类型手写一行调用——新增类型不用再改这里。
		// 传类型下标 t 进去，是为了让 updateFurnitureType 内部知道
		// "现在处理的是哪种类型"，从而判断当前实例是不是 selectedType/selectedIndex 指向的那个，
		// 决定要不要给它加高亮
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++)
			updateFurnitureType(t, *furnitureTypes[t], currentImage);
		
		// updates the FPS
		static float elapsedT = 0.0f;
		static int countedFrames = 0;
		
		countedFrames++;
		elapsedT += deltaT;
		if(elapsedT > 1.0f) {
			float Fps = (float)countedFrames / elapsedT;
			
			std::ostringstream oss;
			oss << "FPS: " << Fps << "\n";

			// 位置要跟 localInit 里第一次 print(id=1) 时保持一致（右上角），
			// 不然每次刷新 FPS 数值时它又会跳回默认位置
			txt.print(1.0f, -1.0f, oss.str(), 1, "CO", false, false, true,TAL_RIGHT,TRH_RIGHT,TRV_TOP,{1.0f,0.0f,0.0f,1.0f},{0.8f,0.8f,0.0f,1.0f});
			
			elapsedT = 0.0f;
		    countedFrames = 0;
		}
		
		txt.updateCommandBuffer();
	}

	// 遍历某种家具类型的所有槽位，计算世界矩阵并写进对应的 Descriptor Set
	void updateFurnitureType(int typeIndex, FurnitureTypeData &type, uint32_t currentImage) {
		UniformBufferObject ubo{};

		// 选中高亮做成"呼吸灯"效果：用 sin() 让强度在 0.15~0.45 之间来回浮动，
		// 比固定强度更容易被人眼注意到（静态的一层颜色叠加很容易被忽略）
		float pulse = 0.30f + 0.15f * sinf((float)glfwGetTime() * 4.0f);

		for (size_t i = 0; i < type.inst.size(); i++) {
			FurnitureInstance &fi = type.inst[i];

			glm::mat4 Wm;
			if (fi.active) {
				// TRS：先旋转再平移（矩阵乘法从右往左作用在顶点上）。
				// baseScale 放在 baseRot 前面(乘在最后)，缩放的是模型自己的局部坐标系，
				// 不会影响 pos 表示的位置——缩放系数只让模型本身变大/变小，摆放的地方不变
				Wm = glm::translate(glm::mat4(1), fi.pos) *
					 glm::rotate(glm::mat4(1), fi.rotY, glm::vec3(0, 1, 0)) *
					 glm::scale(glm::mat4(1), glm::vec3(type.baseScale)) * type.baseRot;
				ubo.nMat = glm::inverse(glm::transpose(Wm));
			} else {
				// 没激活的槽位缩放成 0，GPU 端画出来就是一个点，等于看不见
				Wm = glm::scale(glm::mat4(1), glm::vec3(0.0f));
				ubo.nMat = glm::mat4(1); // 反正不可见，nMat 填什么都没关系
			}

			ubo.mMat     = Wm;
			ubo.mvpMat   = ViewPrj * Wm;
			ubo.specular = glm::vec4(1.0f, 1.0f, 1.0f, 100.0f);
			ubo.ambient  = glm::vec3(1.0f, 1.0f, 1.0f);

			// 当前这个槽位是不是被选中的那一个？只有 typeIndex 和下标 i 同时对上
			// selectedType/selectedIndex 才算选中——这两个变量是点选/生成家具时设置的（见下方 updateFurnitureInteraction）
			bool isSelected = fi.active && typeIndex == selectedType && (int)i == selectedIndex;
			ubo.highlight = isSelected ? glm::vec4(1.0f, 0.85f, 0.15f, pulse) : glm::vec4(0.0f);

			type.DS[i].map(currentImage, &ubo, 0);
		}
	}

	// ── 家具交互：把当前鼠标屏幕坐标，反推成一条 3D 空间里的射线 ──
	// 思路：鼠标点击给的只是一个 2D 屏幕像素坐标，但我们要在 3D 世界里判断点中了什么。
	// 做法是把这个像素坐标反投影成"相机发出、穿过这个像素"的那条 3D 射线，
	// 用射线的起点(origin)和方向(dir)表示，之后不管是"和地面求交"（放置新家具）
	// 还是"离哪个家具最近"（选中已有家具），都基于这同一条射线来算。
	bool computeMouseRay(glm::vec3 &origin, glm::vec3 &dir, const glm::mat4 &Proj, const glm::mat4 &View) {
		double mx, my;
		glfwGetCursorPos(window, &mx, &my);
		int w, h;
		glfwGetWindowSize(window, &w, &h);
		if (w == 0 || h == 0) return false;

		// 屏幕像素坐标 -> NDC（[-1,1]）。
		// 这里的 Proj 已经在 GameLogic() 里做过 Prj[1][1] *= -1（Vulkan 的 Y 轴翻转修正），
		// 所以反投影时 Y 也要用"不翻转"的写法，跟渲染时用的是同一套坐标约定
		float ndcX = (float)(2.0 * mx / w - 1.0);
		float ndcY = (float)(2.0 * my / h - 1.0);

		glm::mat4 invVP = glm::inverse(Proj * View);
		// GLM_FORCE_DEPTH_ZERO_TO_ONE 生效时 Vulkan 深度范围是 [0,1]：z=0 是近平面，z=1 是远平面
		glm::vec4 nearH = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		glm::vec4 farH  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		glm::vec3 nearP = glm::vec3(nearH) / nearH.w;
		glm::vec3 farP  = glm::vec3(farH)  / farH.w;

		origin = nearP;
		dir    = glm::normalize(farP - nearP);
		return true;
	}

	// 找某种家具类型里第一个还空着（未激活）的槽位，返回下标；找不到（全用完了）返回 -1
	int findFreeSlot(FurnitureTypeData &type) {
		for (size_t i = 0; i < type.inst.size(); i++)
			if (!type.inst[i].active) return (int)i;
		return -1;
	}

	// 点图标/按数字键：直接在一个固定的默认位置生成一个新家具，并选中它，
	// 剩下的"移到哪"交给用户自己用 IJKL 完成——不用再多点一次地面来指定生成位置
	void spawnFurniture(int type) {
		// 黑板要贴在后墙的高度上（跟 localInit 里默认黑板的位置一致），
		// 其余几种都是贴地的家具，摆在地面中央附近就行。
		// 第六步（也是新增类型最后一处要改的地方）：这个数组按枚举顺序也要加一行，
		// 是"点图标生成"这条路径专用的默认位置，跟 localInit 里"预置在房间里"的默认位置
		// 是两回事——那边是可选的，这里是必须的（不然新生成的家具不知道该放哪）
		// z=-6.0 是新房间(z: -12~0)的深度中点，跟着房间大小一起同步过；黑板的 z=-11.8 跟
		// localInit 里默认黑板贴的后墙位置(z=-12)保持一致
		// 之前这里漏加了新增的 12 种类型，缺的下标会用 vec3 的默认值 (0,0,0)——
		// 而 (0,0,0) 又刚好埋在默认的一张课桌底下，生成出来的小道具整个被课桌挡住/卡住，
		// 看起来就像点了没反应。现在把新加的类型都单独分配一个远离默认桌椅的空地坐标
		// (房间前部 z=-1~-2 这一片，默认布局没有摆东西，比较空)，一眼就能看到刚生成的东西。
		static const glm::vec3 DEFAULT_SPAWN_POS[FURNITURE_TYPE_COUNT] = {
			glm::vec3(0.0f, 0.0f, -6.0f),   // DESK
			glm::vec3(0.0f, 0.0f, -6.0f),   // CHAIR
			glm::vec3(0.0f, 1.0f, -11.8f),  // BLACKBOARD
			glm::vec3(0.0f, 0.0f, -6.0f),   // SHELF
			glm::vec3(0.0f, 0.0f, -6.0f),   // PODIUM
			glm::vec3(0.0f, 0.0f, -6.0f),   // LOCKER
			glm::vec3(-6.0f, 0.0f, -2.0f),  // DESK2
			glm::vec3(-4.0f, 0.0f, -2.0f),  // CHAIR2
			glm::vec3(-2.0f, 0.0f, -2.0f),  // DESK3
			glm::vec3( 0.0f, 0.0f, -2.0f),  // CHAIR3
			glm::vec3( 2.0f, 0.0f, -2.0f),  // PLANT1
			glm::vec3( 4.0f, 0.0f, -2.0f),  // PLANT2
			glm::vec3( 6.0f, 0.0f, -2.0f),  // PAPER
			glm::vec3(-6.0f, 0.0f, -1.0f),  // CLOCK
			glm::vec3(-3.0f, 0.0f, -1.0f),  // NOTICEBOARD
			glm::vec3( 0.0f, 0.0f, -1.0f),  // RACK2
			glm::vec3( 3.0f, 0.0f, -1.0f),  // BOOK
			glm::vec3( 6.0f, 0.0f, -1.0f),  // LAPTOP
		};

		FurnitureTypeData *fd = furnitureTypes[type];
		int slot = findFreeSlot(*fd);
		if (slot < 0) {
			std::cout << "这种家具的槽位已经用完了（上限在 localInit 里的 MAX_* 常量设定）\n";
			return;
		}
		fd->inst[slot].active = true;
		fd->inst[slot].pos    = DEFAULT_SPAWN_POS[type];
		fd->inst[slot].rotY   = 0.0f;
		selectedType  = type;
		selectedIndex = slot;
	}

	// 在所有 4 种家具类型里，找"离点击射线最近"的、已经放置好的那一个。
	// 不再是"投影到地面再比 XZ"（那样会丢掉高度信息，挂墙上的黑板就选不准），
	// 而是直接算 3D 空间里"射线到家具位置"的最短距离：
	// 把 (家具位置 - 射线起点) 这个向量，投影到射线方向上，就能找到射线上离家具最近的那一点，
	// 这一点到家具位置的距离，就是"这次点击有多准地指向了这个家具"。
	// 这样不管家具是贴地的还是挂在墙上的，只要点击方向对准了它，这个距离就会很小。
	bool findNearestFurnitureByRay(const glm::vec3 &rayOrigin, const glm::vec3 &rayDir,
								   float maxDist, int &outType, int &outIndex) {
		float best = maxDist;
		bool found = false;
		for (int t = 0; t < FURNITURE_TYPE_COUNT; t++) {
			FurnitureTypeData *fd = furnitureTypes[t];
			for (size_t i = 0; i < fd->inst.size(); i++) {
				if (!fd->inst[i].active) continue;

				glm::vec3 toObj = fd->inst[i].pos - rayOrigin;
				float proj = glm::dot(toObj, rayDir); // rayDir 已经是单位向量，这个投影长度就是 t
				if (proj < 0.0f) continue;             // 家具在相机背后，忽略

				glm::vec3 closestOnRay = rayOrigin + proj * rayDir;
				float d = glm::distance(closestOnRay, fd->inst[i].pos);
				if (d < best) { best = d; outType = t; outIndex = (int)i; found = true; }
			}
		}
		return found;
	}

	// 键盘/鼠标驱动的家具交互，每帧调用一次：
	// 点图标：立刻在默认位置生成一个新家具并选中；
	// 鼠标右键/空格：选中离点击射线最近的已有家具（点空地就是取消选中）；
	// 选中之后：IJKL 移动，Q/E（复用相机没用到的 r.z）旋转，X 删除
	void updateFurnitureInteraction(float deltaT, const glm::vec3 &r, const glm::mat4 &Proj, const glm::mat4 &View) {

		// 上升沿检测：把"这一帧按键是否按着"变成"这一帧是不是刚刚按下的那一下"
		// 用在删除键/开火键这种"一次点击=一次动作"的场合，避免按住不放时每帧都重复触发
		auto pressedOnce = [this](int key, bool &prevState) {
			bool now = glfwGetKey(window, key) == GLFW_PRESS;
			bool edge = now && !prevState;
			prevState = now;
			return edge;
		};

		static bool prev0 = false, prevDelete = false, prevFire = false;

		// 数字键 0：取消选中（图标栏没有对应的"取消选中"按钮，先留着这一个键盘操作）
		if (pressedOnce(GLFW_KEY_0, prev0)) { selectedType = selectedIndex = -1; }

		// ── 图标栏点击：左键点在某个图标的方框里，效果跟按对应的数字键一样 ──
		// 左键平时是拖动旋转相机用的，但只是单击一下（没有拖动）的话，
		// getSixAxis 里算出来的旋转量本来就趋近于 0，不会有明显冲突
		static bool prevLeftClick = false;
		bool leftNow  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
		bool leftEdge = leftNow && !prevLeftClick;
		prevLeftClick = leftNow;
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
						// ICON_COUNT 的下标顺序就是 FurnitureType 枚举的顺序（DESK/CHAIR/BLACKBOARD/SHELF）
						spawnFurniture(i);
						break;
					}
				}
			}
		}

		// 选中键：只用鼠标右键（原来空格也能触发，跟右键是"或"的关系，
		// 但空格同时也是别的引擎默认按键，混在一起容易让人搞不清是哪个键生效的，
		// 去掉之后只有右键这一个入口，行为更清楚）。
		// 这里自己查一遍鼠标状态是因为需要做"这一下刚点"的上升沿检测——
		// 每帧查 GLFW 状态拿到的是"当前是否按住"，不区分边沿，得自己拿上一帧的状态来对比
		// 现在只做一件事：选中离点击射线最近的已有家具
		bool fireNow  = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
		bool fireEdge = fireNow && !prevFire;
		prevFire = fireNow;

		if (fireEdge) {
			glm::vec3 rayOrigin, rayDir;
			if (computeMouseRay(rayOrigin, rayDir, Proj, View)) {
				// 用真正的 3D 射线距离找最近的已放置家具，不再局限于贴地的物体，
				// 挂墙上的黑板一样能被准确点中
				int t, i;
				if (findNearestFurnitureByRay(rayOrigin, rayDir, 1.2f, t, i)) {
					selectedType  = t;
					selectedIndex = i;
				} else {
					selectedType = selectedIndex = -1; // 没点中任何家具：取消选中
				}
			}
		}

		// 有选中的家具时：IJKL 前后左右移动、U/O 上下移动、Q/E 旋转、Delete 删除
		if (selectedIndex >= 0) {
			FurnitureInstance &fi = furnitureTypes[selectedType]->inst[selectedIndex];

			const float MOVE_SPEED = 3.0f;
			// 45°/秒转起来太慢了，调快到 100°/秒，手感更跟手
			const float ROT_SPEED  = glm::radians(100.0f);

			if (glfwGetKey(window, GLFW_KEY_I)) fi.pos.z -= MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_K)) fi.pos.z += MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_J)) fi.pos.x -= MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_L)) fi.pos.x += MOVE_SPEED * deltaT;
			// U/O：上下移动（Y 轴），之前只能水平移动，挂墙上的黑板这类东西调高度会用到
			if (glfwGetKey(window, GLFW_KEY_U)) fi.pos.y += MOVE_SPEED * deltaT;
			if (glfwGetKey(window, GLFW_KEY_O)) fi.pos.y -= MOVE_SPEED * deltaT;

			// 边界限制：每次移动完都把位置夹回房间范围内，防止家具被拖出墙外/地板外。
			// 关键点：不能直接夹家具的中心点到墙的位置，要把这种家具自己的
			// halfX/halfZ（localInit 里用 Colliders.hpp 量出来的半宽半深）也减掉，
			// 这样才是"家具的边缘刚好碰到墙"而不是"家具的中心刚好碰到墙"（中心到墙时，
			// 半个家具已经穿出去了——这正是课桌之前露出墙外的原因）。
			// X 用 halfX、Z 用 halfZ 分开算，而不是共用一个圆的半径——黑板这种又宽又薄的
			// 家具如果两个方向共用同一个（更大的）半径，会导致 Z 方向的余量被过度高估，
			// 结果黑板怎么都够不到贴着的后墙。
			// 注意变量名不能叫 r——下面 Q/E 转向要用到的 r.z 是 GameLogic 传进来的
			// glm::vec3 r（这个函数的参数），起同名的局部变量会把它遮住
			FurnitureTypeData *ft = furnitureTypes[selectedType];
			float marginX = ft->halfX + WALL_THICKNESS_MARGIN;
			float marginZ = ft->halfZ + WALL_THICKNESS_MARGIN;
			fi.pos.x = glm::clamp(fi.pos.x, ROOM_MIN_X + marginX, ROOM_MAX_X - marginX);
			fi.pos.y = glm::clamp(fi.pos.y, ROOM_MIN_Y, ROOM_MAX_Y);
			fi.pos.z = glm::clamp(fi.pos.z, ROOM_MIN_Z + marginZ, ROOM_MAX_Z - marginZ);

			// r.z 是 getSixAxis 里 Q/E 控制的量（Q=+1，E=-1），本来是给相机"翻滚"用的，
			// 我们的第三人称相机没有用到翻滚，正好拿来转选中的家具
			fi.rotY += r.z * ROT_SPEED * deltaT;

			// 删除键换成 Delete。注意 Mac 笔记本键盘上标着"delete"的那个键，
			// 传给 GLFW 的其实是 GLFW_KEY_BACKSPACE（真正的 GLFW_KEY_DELETE 是 fn+delete 那个"向前删除"），
			// 两个键都判断一下，不用记是哪一个
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
		// Camera FOV-y, Near Plane and Far Plane
		const float FOVy = glm::radians(45.0f);
		const float nearPlane = 0.1f;
		const float farPlane = 100.f;

		// Integration with the timers and the controllers
		float deltaT;
		glm::vec3 m = glm::vec3(0.0f), r = glm::vec3(0.0f);
		bool fire = false;
		getSixAxis(deltaT, m, r, fire);

		// Projection
		glm::mat4 Prj = glm::perspective(FOVy, Ar, nearPlane, farPlane);
		Prj[1][1] *= -1;

		// View
		// ── 第三人称"规划"相机：不再直接控制相机本身的位置，
		// 而是控制一个 CamTarget（相机看着的目标点，大致是房间里的某处），
		// 相机永远待在 CamTarget 后方 Distance 远的地方看着它。
		static glm::vec3 CamTarget = glm::vec3(0.0f, 0.0f, -4.5f); // 初始对准房间大致中心
		static float Yaw      = 0.0f;
		static float Pitch    = glm::radians(-35.0f); // 带一点俯视角度，符合"规划视角"
		static float Distance = 10.0f;

		const float MOVE_SPEED = 4.0f;
		const float ROT_SPEED  = glm::radians(90.0f);
		const float ZOOM_SPEED = 8.0f;

		// WASD (m.x, m.z)：平移 CamTarget，方向按当前 Yaw 旋转，
		// 只用 Yaw 不用 Pitch，这样平移永远贴着地面走
		// （和之前第一人称相机的写法完全一样，只是移动的对象从 CamPos 换成了 CamTarget）
		glm::vec3 moveDir = glm::vec3(
			glm::rotate(glm::mat4(1), Yaw, glm::vec3(0, 1, 0)) *
			glm::vec4(m.x, 0.0f, m.z, 0.0f));
		CamTarget.x += moveDir.x * MOVE_SPEED * deltaT;
		CamTarget.z += moveDir.z * MOVE_SPEED * deltaT;

		// 方向键/鼠标拖拽 (r.x, r.y)：绕 CamTarget 旋转视角
		Pitch -= r.x * ROT_SPEED * deltaT;
		Yaw   -= r.y * ROT_SPEED * deltaT;
		// 限制俯仰角范围，避免转到房间下方，或者转成完全垂直往下看
		Pitch = glm::clamp(Pitch, glm::radians(-75.0f), glm::radians(-15.0f));

		// R/F (m.y)：原来是升降相机，现在改成缩放（拉近拉远）
		Distance -= m.y * ZOOM_SPEED * deltaT;
		Distance = glm::clamp(Distance, 4.0f, 20.0f);

		// 根据 Yaw/Pitch 反推相机现在"面朝"的方向：
		// 这其实是 View 矩阵旋转部分的逆过程——View 用 Rx(-Pitch)*Ry(-Yaw) 把世界转到相机空间，
		// 这里反过来，用 Ry(Yaw)*Rx(Pitch) 把相机自己的"前方"(0,0,-1) 转回世界坐标系
		glm::vec3 forward = glm::vec3(
			glm::rotate(glm::mat4(1), Yaw, glm::vec3(0, 1, 0)) *
			glm::rotate(glm::mat4(1), Pitch, glm::vec3(1, 0, 0)) *
			glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));

		// 相机站在目标点沿着"面朝方向"的反方向、后退 Distance 的地方
		glm::vec3 CamPos = CamTarget - forward * Distance;

		// Look-in-direction View Matrix（公式和之前第一人称相机完全一样，
		// 只是这里的 CamPos 不再是玩家直接移动的量，而是从 CamTarget 算出来的）
		View = glm::rotate   (glm::mat4(1), -Pitch, glm::vec3(1, 0, 0)) *
			   glm::rotate   (glm::mat4(1), -Yaw,   glm::vec3(0, 1, 0)) *
			   glm::translate(glm::mat4(1), -CamPos);

		// View-Projection
		ViewPrj = Prj * View;

		// 家具的选中/放置/移动/旋转/删除交互，需要用到这一帧刚算好的 Prj/View
		// （鼠标点击要反投影成世界坐标，必须用当前帧的相机矩阵，不能用上一帧的）
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