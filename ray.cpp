#include <algorithm>
#include <assert.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "scoped_timer.h"

struct Color {
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  Color operator*(float scalar) const {
    return {(uint8_t)std::min(255.0f, scalar * red),
            (uint8_t)std::min(255.0f, scalar * green),
            (uint8_t)std::min(255.0f, scalar * blue)};
  }

  Color operator+(Color c) const {
    return {(uint8_t)std::min(255, red + c.red),
            (uint8_t)std::min(255, green + c.green),
            (uint8_t)std::min(255, blue + c.blue)};
  }

  void print() const { printf("color: (%u, %u, %u)\n", red, green, blue); }
};

struct Material {
  Color color;
  float specular_contribution;
  float diffuse_contribution;
  float specular_exponent;
  float reflectance;
};

Color operator*(float scalar, Color c) { return c * scalar; }

struct Image {
  int width, height;
  Color *pixels;
};

struct V3f {
  float x, y, z;

  V3f operator-(V3f b) const { return {x - b.x, y - b.y, z - b.z}; }
  V3f operator-() const { return {-x, -y, -z}; }

  V3f operator+(V3f b) const { return {x + b.x, y + b.y, z + b.z}; }
  V3f operator+() const { return *this; }

  float operator*(V3f b) const { return (x * b.x + y * b.y + z * b.z); };
  V3f operator*(float scalar) const {
    return {scalar * x, scalar * y, scalar * z};
  }

  float length_sq() const {
    V3f A = *this;
    return A * A;
  }
  float length() const { return sqrt(length_sq()); }

  void print() const {
    float x2 = x, y2 = y, z2 = z;
    // Get rid of -0.0
    if (x2 == 0)
      x2 = 0;
    if (y2 == 0)
      y2 = 0;
    if (z2 == 0)
      z2 = 0;
    printf("(%.1f, %.1f, %.1f)\n", x2, y2, z2);
  }
};

V3f operator*(float scalar, V3f v) { return v * scalar; }
V3f normalize(V3f v) { return (1 / v.length()) * v; }

struct Plane {
  V3f normal;
  float distance;
  Material mat;
};

struct Sphere {
  V3f center;
  float radius;
  Material mat;
};

struct Ray {
  V3f origin;
  V3f direction;
};

struct CoordinateSpace {
  V3f origin;
  V3f x_axis, y_axis, z_axis;

  // Get a vector in world space which is expressed
  // with coordinates in this coordinate space.
  V3f vector(float x, float y, float z) const {
    return origin + x*x_axis + y*y_axis + z*z_axis;
  }

  void print() const {
    printf("x_axis: ");
    x_axis.print();
    printf("y_axis: ");
    y_axis.print();
    printf("z_axis: ");
    z_axis.print();
  }
};

struct Light {
  V3f position;
  float intensity;
};

struct World {
  static constexpr int max_num_planes = 3;
  static constexpr int max_num_spheres = 5;
  static constexpr int max_num_lights = 3;

  int num_planes;
  Plane planes[max_num_planes];
  int num_spheres;
  Sphere spheres[max_num_spheres];
  int num_lights;
  Light lights[max_num_lights];
  Color default_color;
  CoordinateSpace camera;

  World() {
    num_planes = 0;
    num_spheres = 0;
    num_lights = 0;
  }

  void push_plane(Plane plane) {
    assert(num_planes < max_num_planes);
    planes[num_planes] = plane;
    ++num_planes;
  }

  void push_sphere(Sphere sphere) {
    assert(num_spheres < max_num_spheres);
    spheres[num_spheres] = sphere;
    ++num_spheres;
  }

  void push_light(Light light) {
    assert(num_lights < max_num_lights);
    lights[num_lights] = light;
    ++num_lights;
  }
};

V3f cross(V3f a, V3f b) {
  V3f res;
  res.x = a.y * b.z - a.z * b.y;
  res.y = a.z * b.x - a.x * b.z;
  res.z = a.x * b.y - a.y * b.x;
  return res;
}

Image allocate_image(int width, int height) {
  Image res;
  // Get one contiguous chunk of memory.
  res.pixels = (Color *)malloc(width * height * sizeof(Color));
  res.width = width;
  res.height = height;
  return res;
}

void write_image_to_file(Image img) {
  FILE *fp = fopen("out.ppm", "wb+");
  fprintf(fp, "P6\n%d %d\n255\n", img.width, img.height);
  fwrite(img.pixels, 1, img.width * img.height * sizeof(Color), fp);
  fclose(fp);
}

bool float_is_zero(float v) {
  constexpr float tolerance = 0.0001f;
  return (-tolerance < v && v < tolerance);
}

bool float_is_negative(float v) {
  constexpr float tolerance = 0.0001f;
  return (v < tolerance);
}

// If \p ray intersects \p plane, then return the intersection
// distance. Otherwise, return \p def.
float ray_intersects_plane(const Ray *ray, const Plane *plane, float def) {
  float denom = plane->normal * ray->direction;
  bool plane_normal_and_ray_direction_are_perpendicular = float_is_zero(denom);
  if (plane_normal_and_ray_direction_are_perpendicular)
    return def;
  return (plane->distance - (plane->normal * ray->origin)) / denom;
}

// If \p ray intersects \p sphere, then return the intersection
// distance. Otherwise, return \p def.
float ray_intersects_sphere(const Ray *ray, const Sphere *sphere, float def) {
  // Helpful variable
  V3f v = ray->origin - sphere->center;
  float a = ray->direction * ray->direction;
  assert(!float_is_zero(a));
  float b = 2 * v * ray->direction;
  float c = v * v - sphere->radius;

  float denom = 2 * a; // Can't be 0 since `a` can't be zero.
  float discriminant = sqrt(b * b - 4.0f * a * c);
  if (float_is_negative(discriminant))
    // No solution exists - i.e. the ray does not hit the sphere in any way.
    return def;

  float x1 = (-b + discriminant) / denom;
  float x2 = (-b - discriminant) / denom;

  float res = (x1 > 0) ? x1 : FLT_MAX;
  if (x2 > 0 && x2 < res)
    res = x2;
  return res;
}

// Reflect \p v by \p normal.
// Note: v and normal must be normalized. Also, this mathematical
// operation produces a normalized vector.
V3f reflect(V3f v, V3f normal) { return (v - 2.0f * (v * normal) * normal); }

struct RayIntersectionResult {
  V3f normal, intersection_point;
  Material mat;
  bool intersected;
};

RayIntersectionResult intersect_ray_with_world(const Ray *ray, const World *world) {
  RayIntersectionResult res;
  res.intersected = false;

  // For all the objects that \p ray intersects, find the intersection
  // distance. The resulting color is of the object that has the minimum
  // intersection distance.

  float min_intersection_distance = FLT_MAX;
  for (int i = 0; i < world->num_planes; ++i) {
    const Plane *plane = &world->planes[i];
    float intersection_distance = ray_intersects_plane(ray, plane, FLT_MAX);
    if (intersection_distance >= 0.0f &&
        intersection_distance < min_intersection_distance) {
      min_intersection_distance = intersection_distance;
      res.intersected = true;
      res.mat = plane->mat;
      res.intersection_point =
          ray->origin + min_intersection_distance * ray->direction;
      res.normal = plane->normal;
    }
  }

  for (int i = 0; i < world->num_spheres; ++i) {
    const Sphere *sphere = &world->spheres[i];
    float intersection_distance = ray_intersects_sphere(ray, sphere, FLT_MAX);
    // We have to account for negative intersection dinstance as in the planes.
    if (intersection_distance >= 0.0f &&
        intersection_distance < min_intersection_distance) {
      min_intersection_distance = intersection_distance;
      res.intersected = true;
      res.mat = sphere->mat;
      res.intersection_point =
          ray->origin + min_intersection_distance * ray->direction;
      res.normal = normalize(res.intersection_point - sphere->center);
    }
  }

  return res;
}

Color cast_ray(const Ray *ray, const World *world, int reflection_depth = 0) {
  Color res = world->default_color;

  if (reflection_depth > 3)
    return res;

  RayIntersectionResult intersection_res = intersect_ray_with_world(ray, world);

  // Apply lights using the Phong reflection model + simple shadow computation
  // + reflections.
  if (intersection_res.intersected) {
    Material mat = intersection_res.mat;
    res = mat.color;
    float diffuse_intensity = 0.0f;
    float specular_intensity = 0.0f;
    V3f point = intersection_res.intersection_point;
    V3f normal = intersection_res.normal;
    for (int i = 0; i < world->num_lights; ++i) {
      Light light = world->lights[i];
      V3f light_dir = normalize(light.position - point);

      // If there's an interleaving object _from_ the
      // intersection point _to_ the light source (which
      // we check by creating a ray), then we skip this
      // light source (which effectively applies shadows).

      // "Move" the starting point a little bit because
      // notice that all the starting points are intersection
      // points. Intersection points are _exactly on_ objects
      // in the world. So, if we start a ray from such a point,
      // this ray will intersect with that object.
      V3f shadow_starting_point = point + light_dir * 1e-3;
      Ray light_ray = {shadow_starting_point, light_dir};
      RayIntersectionResult light_intersect =
          intersect_ray_with_world(&light_ray, world);
      if (light_intersect.intersected)
        continue;
      diffuse_intensity +=
          light.intensity * std::fmax(0.0f, light_dir * normal);
      float temp = fmax(0.0f, reflect(light_dir, normal) * ray->direction);
      specular_intensity += pow(temp, mat.specular_exponent) * light.intensity;
    }

    // Recursively compute the reflection color.
    // Note: Doing this with a loop would be harder it seems.
    Color reflect_color = world->default_color;
    if (!float_is_zero(mat.reflectance)) {
      V3f reflect_dir = reflect(ray->direction, normal);
      V3f reflect_starting_point = point + reflect_dir * 1e-3;
      Ray reflect_ray = {reflect_starting_point, reflect_dir};
      reflect_color = cast_ray(&reflect_ray, world, reflection_depth + 1);
    }
    float diffuse_component = diffuse_intensity * mat.diffuse_contribution;
    float specular_component = specular_intensity * mat.specular_contribution;
    Color specular_color = {255, 255, 255};
    res = mat.color * diffuse_component +
          specular_color * specular_intensity * mat.specular_contribution +
          mat.reflectance * reflect_color;
  }

  return res;
}

struct TileInfo {
  pthread_t id;
  Image img;
  int ymin, ymax;
  const World *world;
};

void *render_tile(void *info) {
  TileInfo *tile_info = (TileInfo *)info;
  const World *world = tile_info->world;
  Image img = tile_info->img;
  int ymin = tile_info->ymin;
  int ymax = tile_info->ymax;
  int xmin = 0;
  int xmax = img.width;

  CoordinateSpace camera = world->camera;

  float half_film_width = 1.0f;
  float half_film_height = 1.0f;
  // Aspect ratio computation. Assume that the maximum width is 1.0f
  // which will be held for the bigger dimension (if there's one).
  // We'll shrink the smaller one accordingly.
  if (img.width > img.height)
    half_film_height = (float)img.height / (float)img.width;
  else if (img.height > img.width)
    half_film_width = (float)img.width / (float)img.height;

  constexpr float FOV = 1.0f;
  V3f film_center = camera.origin + FOV * camera.z_axis;
  // Construct a coordinate space for the film so that we can express points in it.
  CoordinateSpace film = {film_center, camera.x_axis, camera.y_axis, camera.z_axis};
  
  Color *pixels = &img.pixels[ymin * img.width + xmin];
  float ystep = 2.0f / (float)img.height;
  float film_y = 1.0f - ymin*ystep;
  float xstep = 2.0f / (float)img.width;
  for (int y = ymin; y != ymax; ++y) {
    float film_x = -1.0f + xmin*xstep;
    for (int x = xmin; x != xmax; ++x) {
      V3f film_point = film.vector(half_film_width*film_x, half_film_height*film_y, 0.0f);

      // Cast a ray.
      Ray ray;
      // Start from the camera origin
      ray.origin = camera.origin;
      // and cast it through the film point.
      ray.direction = normalize(film_point - ray.origin);

      img.pixels[y * img.width + x] = cast_ray(&ray, world);

      film_x += xstep;
    }
    film_y -= ystep;
  }

  pthread_exit(NULL);
}

void render_world(World *world, Image img) {
  MEASURE_SCOPE("render world");
  // Assumptions:
  // 1) The number of cores is a power of 2 (almost always true).
  // 2) The image dimensions can be divided evenly (not always true,
  //    but for a 4K it will work for most machines).

  // Divide only in height to get cache locality and prevent false sharing.
  int nthreads = sysconf(_SC_NPROCESSORS_ONLN);
  int tile_height = img.height / nthreads;
  // Assumption 2)
  assert(img.height % nthreads == 0);
  TileInfo *tile_infos = new TileInfo[nthreads];
  int ymin = 0;
  for (int thread = 0; thread < nthreads; ++thread) {
    tile_infos[thread].world = world;
    tile_infos[thread].img = img;
    tile_infos[thread].ymin = ymin;
    tile_infos[thread].ymax = ymin + tile_height;
    pthread_create(&tile_infos[thread].id, NULL, render_tile,
                   &tile_infos[thread]);
    ymin += tile_height;
  }

  for (int thread = 0; thread < nthreads; ++thread) {
    pthread_join(tile_infos[thread].id, NULL);
  }

  write_image_to_file(img);
}

int main() {
  constexpr int width = 4096;
  constexpr int height = 2160;
  Image img = allocate_image(width, height);

  CoordinateSpace camera;
  camera.origin = {0, 6, -8};
  camera.z_axis = normalize(-camera.origin);
  camera.x_axis = normalize(cross(V3f{0, 1, 0}, camera.z_axis));
  camera.y_axis = normalize(cross(camera.z_axis, camera.x_axis));

  World world;
  Color alice_blue = {240, 248, 255};
  Color redish = {203, 65, 84};
  Color aero_blue = {124, 185, 232};
  Color light_purple = {124, 105, 232};
  Color white = {255, 255, 255};
  Color black = {0, 0, 0};
  world.default_color = alice_blue;
  world.camera = camera;

  // Add spheres
  Sphere sphere;
  sphere = {{0, 0, 0}, 3, {redish, 0.1f, 0.9f, 10, 0.0f}};
  world.push_sphere(sphere);
  sphere = {{-3, 0, 4}, 3, {aero_blue, 0.4f, 0.7f, 50, 0.0f}};
  world.push_sphere(sphere);
  sphere = {{-4, 2, 0}, 3, {light_purple, 0.2f, 0.8f, 70, 0.0f}};
  world.push_sphere(sphere);
  sphere = {{4, 2, 0}, 3, {{255, 255, 255}, 0.0f, 0.0f, 100, 0.8f}};
  world.push_sphere(sphere);
  sphere = {{2, 0, 5}, 3, {{0, 0, 0}, 0.0f, 0.0f, 100, 0.8f}};
  world.push_sphere(sphere);

  // Add planes
  Plane plane = {{0, 1, 0}, -7, {alice_blue, 0.3, 0.7, 20, 0.1}};
  world.push_plane(plane);

  // Add lights
  Light light;
  light = {{-7, 15, -7}, 1.5};
  world.push_light(light);
  light = {{27, 15, 10}, 1.5};
  world.push_light(light);
  light = {{0, -15, 0}, 1.5};
  world.push_light(light);

  render_world(&world, img);

  return 0;
}
