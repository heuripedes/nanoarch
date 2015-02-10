
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <dlfcn.h>

#include "libretro.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <alsa/asoundlib.h>

static GLFWwindow *g_win = NULL;
static snd_pcm_t *g_pcm = NULL;
static float g_scale = 3;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;
static GLuint g_shader_program = 0;
static GLint g_pos_attrib = 0;
static GLint g_coord_attrib = 0;
static GLint g_tex_uniform = 0;

static GLfloat g_vertex_data[] = {
	// vertex
	-1, -1, // left-bottom
	-1,  1, // left-top
	 1, -1, // right-bottom
	 1,  1, // right-top
	// texture
	 0,  1,
	 0,  0,
	 1,  1,
	 1,  0,
};

static struct {
	GLuint tex_id;
	GLuint pitch;
	GLint tex_w, tex_h;
	GLuint clip_w, clip_h;

	GLuint pixfmt;
	GLuint pixtype;
	GLuint bpp;

} g_video  = {0};


static struct {
	void *handle;
	bool initialized;
	bool game_loaded;

	struct retro_system_info sys_info;
	struct retro_system_av_info av_info;
	struct retro_game_info game_info;

	void (*retro_init)(void);
	void (*retro_deinit)(void);
	unsigned (*retro_api_version)(void);
	void (*retro_get_system_info)(struct retro_system_info *info);
	void (*retro_get_system_av_info)(struct retro_system_av_info *info);
	void (*retro_set_controller_port_device)(unsigned port, unsigned device);
	void (*retro_reset)(void);
	void (*retro_run)(void);
//	size_t retro_serialize_size(void);
//	bool retro_serialize(void *data, size_t size);
//	bool retro_unserialize(const void *data, size_t size);
//	void retro_cheat_reset(void);
//	void retro_cheat_set(unsigned index, bool enabled, const char *code);
	bool (*retro_load_game)(const struct retro_game_info *game);
//	bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info);
	void (*retro_unload_game)(void);
//	unsigned retro_get_region(void);
//	void *retro_get_memory_data(unsigned id);
//	size_t retro_get_memory_size(unsigned id);
} g_retro;

struct keymap {
	unsigned k;
	unsigned rk;
};

struct keymap g_binds[] = {
	{ GLFW_KEY_X, RETRO_DEVICE_ID_JOYPAD_A },
	{ GLFW_KEY_Z, RETRO_DEVICE_ID_JOYPAD_B },
	{ GLFW_KEY_A, RETRO_DEVICE_ID_JOYPAD_Y },
	{ GLFW_KEY_S, RETRO_DEVICE_ID_JOYPAD_X },
	{ GLFW_KEY_UP, RETRO_DEVICE_ID_JOYPAD_UP },
	{ GLFW_KEY_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN },
	{ GLFW_KEY_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT },
	{ GLFW_KEY_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
	{ GLFW_KEY_ENTER, RETRO_DEVICE_ID_JOYPAD_START },
	{ GLFW_KEY_BACKSPACE, RETRO_DEVICE_ID_JOYPAD_SELECT },

	{ 0, 0 }
};

static unsigned g_joy[RETRO_DEVICE_ID_JOYPAD_R3+1] = { 0 };

static const char *g_vshader_src =
	"#version 120\n"
	"attribute vec2 in_pos;\n"
	"attribute vec2 in_coord;\n"
	"varying vec2 var_coord;\n"
	"void main() {\n"
		"var_coord = in_coord;\n"
		"gl_Position = vec4(in_pos, 0.0, 1.0);// * ftransform();\n"
	"}";

static const char *g_fshader_src =
	"#version 120\n"
	"varying vec2 var_coord;\n"
	"uniform sampler2D uni_tex;\n"
	"void main() {\n"
		"gl_FragColor = texture2D(uni_tex, var_coord);\n"
	"}";


#define load_sym(V, S) do {\
	if (!((*(void**)&V) = dlsym(g_retro.handle, #S))) \
		die("Failed to load symbol '" #S "'': %s", dlerror()); \
	} while (0)
#define load_retro_sym(S) load_sym(g_retro.S, S)


static void die(const char *fmt, ...) {
	char buffer[4096];

	va_list va;
	va_start(va, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);

	fputs(buffer, stderr);
	fputc('\n', stderr);
	fflush(stderr);

	exit(EXIT_FAILURE);
}


static GLuint compile_shader(unsigned type, unsigned count, const char **strings) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, count, strings, NULL);
	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

	if (status == GL_FALSE) {
		char buffer[4096];
		glGetShaderInfoLog(shader, sizeof(buffer), NULL, buffer);
		die("Failed to compile %s shader: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", buffer);
	}

	return shader;
}


static GLuint setup_shaders() {
	GLuint vshader = compile_shader(GL_VERTEX_SHADER, 1, &g_vshader_src);
	GLuint fshader = compile_shader(GL_FRAGMENT_SHADER, 1, &g_fshader_src);
	GLuint program = glCreateProgram();
	glAttachShader(program, vshader);
	glAttachShader(program, fshader);
	glLinkProgram(program);

	glDeleteShader(vshader);
	glDeleteShader(fshader);

	glValidateProgram(program);

	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);

	if(status == GL_FALSE) {
		char buffer[4096];
		glGetProgramInfoLog(program, sizeof(buffer), NULL, buffer);
		die("Failed to link shader program: %s", buffer);
	}

	g_pos_attrib = glGetAttribLocation(program, "in_pos");
	g_coord_attrib = glGetAttribLocation(program, "in_coord");
	g_tex_uniform = glGetUniformLocation(program, "uni_tex");

	assert(g_pos_attrib != -1);
	assert(g_coord_attrib != -1);
	assert(g_tex_uniform != -1);

	return program;
}


static void refresh_vertex_data() {
	assert(g_video.tex_w);
	assert(g_video.tex_h);
	assert(g_video.clip_w);
	assert(g_video.clip_h);

	GLfloat *coords = &g_vertex_data[8];
	coords[1] = coords[5] = (float)g_video.clip_h / g_video.tex_h;
	coords[4] = coords[6] = (float)g_video.clip_w / g_video.tex_w;

	if (!g_vao)
		glGenVertexArrays(1, &g_vao);

	glBindVertexArray(g_vao);

	if (!g_vbo)
		glGenBuffers(1, &g_vbo);

	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(g_vertex_data), g_vertex_data, GL_STREAM_DRAW);

	glEnableVertexAttribArray(g_pos_attrib);
	glVertexAttribPointer(g_pos_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);

	glEnableVertexAttribArray(g_coord_attrib);
	glVertexAttribPointer(g_coord_attrib, 2, GL_FLOAT, GL_FALSE, 0, (void*)(8 * sizeof(GLfloat)));

	glBindVertexArray(0);
}


static void resize_cb(GLFWwindow *win, int w, int h) {
	glViewport(0, 0, w, h);
}


static void create_window(int width, int height) {
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

	g_win = glfwCreateWindow(width, height, "nanoarch", NULL, NULL);

	if (!g_win)
		die("Failed to create window.");

	glfwSetFramebufferSizeCallback(g_win, resize_cb);

	glfwMakeContextCurrent(g_win);

	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
		die("Failed to initialize glew");

	glfwSwapInterval(1);

	printf("GLSL Version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	g_shader_program = setup_shaders();

	glUseProgram(g_shader_program);
	glUniform1i(g_tex_uniform, 0);

//	refresh_vertex_data();

	glUseProgram(0);

	resize_cb(g_win, width, height);
}


static void resize_to_aspect(double ratio, int sw, int sh, int *dw, int *dh) {
	*dw = sw;
	*dh = sh;

	if (ratio <= 0)
		ratio = (double)sw / sh;

	if ((float)sw / sh < 1)
		*dw = *dh * ratio;
	else
		*dh = *dw / ratio;
}


static void video_configure(const struct retro_game_geometry *geom) {
	int nwidth, nheight;

	resize_to_aspect(geom->aspect_ratio, geom->base_width * 1, geom->base_height * 1, &nwidth, &nheight);

	nwidth *= g_scale;
	nheight *= g_scale;

	if (!g_win)
		create_window(nwidth, nheight);

	if (g_video.tex_id)
		glDeleteTextures(1, &g_video.tex_id);

	g_video.tex_id = 0;

	if (!g_video.pixfmt)
		g_video.pixfmt = GL_UNSIGNED_SHORT_5_5_5_1;

	glfwSetWindowSize(g_win, nwidth, nheight);

	glGenTextures(1, &g_video.tex_id);

	if (!g_video.tex_id)
		die("Failed to create the video texture");

	g_video.pitch = geom->base_width * g_video.bpp;

	glBindTexture(GL_TEXTURE_2D, g_video.tex_id);

//	glPixelStorei(GL_UNPACK_ALIGNMENT, s_video.pixfmt == GL_UNSIGNED_INT_8_8_8_8_REV ? 4 : 2);
//	glPixelStorei(GL_UNPACK_ROW_LENGTH, s_video.pitch / s_video.bpp);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, geom->max_width, geom->max_height, 0,
			g_video.pixtype, g_video.pixfmt, NULL);

	glBindTexture(GL_TEXTURE_2D, 0);

	g_video.tex_w = geom->max_width;
	g_video.tex_h = geom->max_height;
	g_video.clip_w = geom->base_width;
	g_video.clip_h = geom->base_height;

	refresh_vertex_data();
}


static bool video_set_pixel_format(unsigned format) {
	if (g_video.tex_id)
		die("Tried to change pixel format after initialization.");

	switch (format) {
	case RETRO_PIXEL_FORMAT_0RGB1555:
		g_video.pixfmt = GL_UNSIGNED_SHORT_5_5_5_1;
		g_video.pixtype = GL_BGRA;
		g_video.bpp = sizeof(uint16_t);
		break;
	case RETRO_PIXEL_FORMAT_XRGB8888:
		g_video.pixfmt = GL_UNSIGNED_INT_8_8_8_8_REV;
		g_video.pixtype = GL_BGRA;
		g_video.bpp = sizeof(uint32_t);
		break;
	case RETRO_PIXEL_FORMAT_RGB565:
		g_video.pixfmt  = GL_UNSIGNED_SHORT_5_6_5;
		g_video.pixtype = GL_RGB;
		g_video.bpp = sizeof(uint16_t);
		break;
	default:
		die("Unknown pixel type %u", format);
	}

	return true;
}


static void video_refresh(const void *data, unsigned width, unsigned height, unsigned pitch) {
	if (g_video.clip_w != width || g_video.clip_h != height) {
		g_video.clip_h = height;
		g_video.clip_w = width;

		refresh_vertex_data();
	}

	glBindTexture(GL_TEXTURE_2D, g_video.tex_id);

	if (pitch != g_video.pitch) {
		g_video.pitch = pitch;
		glPixelStorei(GL_UNPACK_ROW_LENGTH, g_video.pitch / g_video.bpp);
	}

	if (data) {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
						g_video.pixtype, g_video.pixfmt, data);
	}

	glBindTexture(GL_TEXTURE_2D, 0);
}


static void video_render() {
	glUseProgram(g_shader_program);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, g_video.tex_id);

	glBindVertexArray(g_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);

	glBindTexture(GL_TEXTURE_2D, 0);

	glUseProgram(0);
}


static void video_deinit() {
	if (g_video.tex_id)
		glDeleteTextures(1, &g_video.tex_id);

	glDeleteVertexArrays(1, &g_vao);
	glDeleteBuffers(1, &g_vbo);
	glDeleteProgram(g_shader_program);

	g_video.tex_id = 0;
}


static void audio_init(int frequency) {
	int err;

	if ((err = snd_pcm_open(&g_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
		die("Failed to open playback device: %s", snd_strerror(err));

	err = snd_pcm_set_params(g_pcm, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED, 2, frequency, 1, 64 * 1000);

	if (err < 0)
		die("Failed to configure playback device: %s", snd_strerror(err));
}


static void audio_deinit() {
	snd_pcm_close(g_pcm);
}


static size_t audio_write(const void *buf, unsigned frames) {
	int written = snd_pcm_writei(g_pcm, buf, frames);

	if (written < 0) {
		printf("Alsa warning/error #%i: ", -written);
		snd_pcm_recover(g_pcm, written, 0);

		return 0;
	}

	return written;
}


static void core_log(enum retro_log_level level, const char *fmt, ...) {
	char buffer[4096];

	va_list va;
	va_start(va, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);

	static const char * levelstr[] = { "dbg", "inf", "wrn", "err" };

	if (level == 0)
		return;

	fprintf(stderr, "[%s] %s", levelstr[level], buffer);

	if (buffer[strlen(buffer)-1] != '\n')
		fputc('\n', stderr);

	fflush(stderr);

	if (level == RETRO_LOG_ERROR)
		exit(EXIT_FAILURE);
}


static bool core_environment(unsigned cmd, void *data) {
	bool *bval;

	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
		struct retro_log_callback *cb = (struct retro_log_callback *)data;
		cb->log = core_log;
		break;
	}
	case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
		bval = (bool*)data;
		*bval = true;
		break;
	}
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		const enum retro_pixel_format *fmt = (enum retro_pixel_format *)data;

		if (*fmt > RETRO_PIXEL_FORMAT_RGB565)
			return false;

		return video_set_pixel_format(*fmt);
	}
	default:
		core_log(RETRO_LOG_DEBUG, "Unhandled env #%u", cmd);
		return false;
	}

	return true;
}


static void core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
	if (data)
		video_refresh(data, width, height, pitch);
}


static void core_input_poll(void) {
	int i;
	for (i = 0; g_binds[i].k || g_binds[i].rk; ++i) {
		g_joy[g_binds[i].rk] = (glfwGetKey(g_win, g_binds[i].k) == GLFW_PRESS);
	}
}


static int16_t core_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
	if (port || index || device != RETRO_DEVICE_JOYPAD)
		return 0;

	return g_joy[id];
}


static void core_audio_sample(int16_t left, int16_t right) {
	int16_t buf[2] = {left, right};
	audio_write(buf, 1);
}


static size_t core_audio_sample_batch(const int16_t *data, size_t frames) {
	return audio_write(data, frames);
}


static void core_load(const char *sofile) {
	memset(&g_retro, 0, sizeof(g_retro));
	g_retro.handle = dlopen(sofile, RTLD_LAZY);

	if (!g_retro.handle)
		die("Failed to load core: %s", dlerror());

	dlerror();

	load_retro_sym(retro_init);
	load_retro_sym(retro_deinit);
	load_retro_sym(retro_api_version);
	load_retro_sym(retro_get_system_info);
	load_retro_sym(retro_get_system_av_info);
	load_retro_sym(retro_set_controller_port_device);
	load_retro_sym(retro_reset);
	load_retro_sym(retro_run);
	load_retro_sym(retro_load_game);
	load_retro_sym(retro_unload_game);

	void (*set_environment)(retro_environment_t);
	load_sym(set_environment, retro_set_environment);
	set_environment(core_environment);

	struct retro_system_info *info = &g_retro.sys_info;
	g_retro.retro_get_system_info(info);

	void (*set_video_refresh)(retro_video_refresh_t);
	load_sym(set_video_refresh, retro_set_video_refresh);
	set_video_refresh(core_video_refresh);

	void (*set_input_poll)(retro_input_poll_t);
	load_sym(set_input_poll, retro_set_input_poll);
	set_input_poll(core_input_poll);

	void (*set_input_state)(retro_input_state_t);
	load_sym(set_input_state, retro_set_input_state);
	set_input_state(core_input_state);

	void (*set_audio_sample)(retro_audio_sample_t);
	load_sym(set_audio_sample, retro_set_audio_sample);
	set_audio_sample(core_audio_sample);

	void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
	load_sym(set_audio_sample_batch, retro_set_audio_sample_batch);
	set_audio_sample_batch(core_audio_sample_batch);

	g_retro.retro_init();
	g_retro.initialized = true;

	char buf[100];
	snprintf(buf, sizeof(buf), "%s %s", info->library_name, info->library_version);
	buf[sizeof(buf)-1] = 0;

	printf("Libretro v%u core loaded: %s\n", g_retro.retro_api_version(), buf);
}


static void core_load_game(const char *filename) {
	struct retro_game_info *game = &g_retro.game_info;
	game->path  = filename;
	game->meta = "";
	game->data = 0;

	FILE *file = fopen(filename, "rb");

	if (!file)
		die("Failed to open content '%s': %s", filename, strerror(errno));

	fseek(file, 0, SEEK_END);
	game->size = ftell(file);
	rewind(file);

	if (!g_retro.sys_info.need_fullpath) {
		void *data = malloc(game->size);

		if (!fread(data, game->size, 1, file))
			die("Failed to read content '%s': %s", filename, strerror(errno));

		game->data = data;
	}

	if (!g_retro.retro_load_game(game))
		die("The core failed to load the content.");

	struct retro_system_av_info *av = &g_retro.av_info;
	g_retro.retro_get_system_av_info(av);

	video_configure(&av->geometry);

	audio_init(av->timing.sample_rate);
}


static void core_unload() {
	if (g_retro.initialized)
		g_retro.retro_deinit();

	if (g_retro.handle)
		dlclose(g_retro.handle);
}

static void error_cb(int err, const char *message) {
	die("GLFW error #%i: %s", err, message);
}

int main(int argc, char *argv[]) {
	if (argc < 3)
		die("usage: %s <core> <game>", argv[0]);

	glfwSetErrorCallback(error_cb);

	if (!glfwInit())
		die("Failed to initialize glfw");

	core_load(argv[1]);
	core_load_game(argv[2]);

	while (!glfwWindowShouldClose(g_win)) {
		glfwPollEvents();

		g_retro.retro_run();

		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);

		video_render();

		glfwSwapBuffers(g_win);
	}

	core_unload();
	audio_deinit();
	video_deinit();

	glfwTerminate();
	return 0;
}
