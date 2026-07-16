/****************************************************************************
 * D13x home panel Mijia family model
 ****************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <netutils/cJSON.h>

#include "home_panel_mijia_model.h"

static void model_copy(char *destination, size_t capacity,
                       const char *source)
{
  snprintf(destination, capacity, "%s", source == NULL ? "" : source);
}

static const char *model_string(cJSON *object, const char *name)
{
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

  return cJSON_IsString(item) && item->valuestring != NULL ?
         item->valuestring : NULL;
}

static cJSON *model_property(cJSON *properties, const char *name)
{
  cJSON *property;

  cJSON_ArrayForEach(property, properties)
    {
      const char *property_name = model_string(property, "name");
      if (property_name != NULL && strcmp(property_name, name) == 0)
        {
          return property;
        }
    }

  return NULL;
}

static bool model_number(cJSON *property, int *value)
{
  cJSON *item;

  if (property == NULL)
    {
      return false;
    }

  item = cJSON_GetObjectItemCaseSensitive(property, "current_value");
  if (!cJSON_IsNumber(item))
    {
      return false;
    }

  *value = item->valueint;
  return true;
}

static bool model_boolean(cJSON *property, bool *value)
{
  cJSON *item;

  if (property == NULL)
    {
      return false;
    }

  item = cJSON_GetObjectItemCaseSensitive(property, "current_value");
  if (!cJSON_IsBool(item))
    {
      return false;
    }

  *value = cJSON_IsTrue(item);
  return true;
}

static bool model_writable(cJSON *property)
{
  const char *rw = property == NULL ? NULL : model_string(property, "rw");

  return rw != NULL && strchr(rw, 'w') != NULL;
}

static void model_brightness_range(cJSON *property,
                                   struct home_panel_device_s *device)
{
  cJSON *range;
  cJSON *minimum;
  cJSON *maximum;

  range = cJSON_GetObjectItemCaseSensitive(property, "range");
  minimum = cJSON_IsArray(range) ? cJSON_GetArrayItem(range, 0) : NULL;
  maximum = cJSON_IsArray(range) ? cJSON_GetArrayItem(range, 1) : NULL;
  device->brightness_min = cJSON_IsNumber(minimum) ? minimum->valueint : 1;
  device->brightness_max = cJSON_IsNumber(maximum) ? maximum->valueint : 100;
}

static void model_parse_device(cJSON *object,
                               struct home_panel_device_s *device)
{
  cJSON *properties;
  cJSON *item;
  int value;

  model_copy(device->did, sizeof(device->did), model_string(object, "did"));
  model_copy(device->name, sizeof(device->name),
             model_string(object, "name"));
  model_copy(device->room, sizeof(device->room),
             model_string(object, "room_name"));
  model_copy(device->model, sizeof(device->model),
             model_string(object, "model"));
  model_copy(device->type, sizeof(device->type),
             model_string(object, "device_type"));
  item = cJSON_GetObjectItemCaseSensitive(object, "isOnline");
  device->online = cJSON_IsTrue(item);

  properties = cJSON_GetObjectItemCaseSensitive(object, "properties");
  if (!cJSON_IsArray(properties))
    {
      return;
    }

  item = model_property(properties, "on");
  device->has_power = model_boolean(item, &device->power);
  device->power_writable = device->has_power && model_writable(item);

  item = model_property(properties, "brightness");
  if (model_number(item, &value))
    {
      device->has_brightness = true;
      device->brightness = (int)value;
      model_brightness_range(item, device);
    }

  item = model_property(properties, "temperature");
  if (model_number(item, &value))
    {
      device->has_temperature = true;
      device->temperature = value;
    }

  item = model_property(properties, "relative-humidity");
  if (model_number(item, &value))
    {
      device->has_humidity = true;
      device->humidity = value;
    }

  item = model_property(properties, "battery-level");
  if (model_number(item, &value))
    {
      device->has_battery = true;
      device->battery = (int)value;
    }
}

static struct home_panel_room_s *model_find_room(
  struct home_panel_family_model_s *model, const char *name)
{
  unsigned int index;

  for (index = 0; index < model->room_count; index++)
    {
      if (strcmp(model->rooms[index].name, name) == 0)
        {
          return &model->rooms[index];
        }
    }

  return NULL;
}

int home_panel_mijia_model_parse(const char *json, uint32_t revision,
                                 struct home_panel_family_model_s *model)
{
  cJSON *devices;
  cJSON *homes;
  cJSON *object;
  cJSON *protocol;
  cJSON *root;
  cJSON *rooms;
  cJSON *scenes;
  unsigned int index;

  if (json == NULL || model == NULL)
    {
      return -EINVAL;
    }

  root = cJSON_Parse(json);
  if (root == NULL)
    {
      return -EBADMSG;
    }

  memset(model, 0, sizeof(*model));
  model->revision = revision;
  protocol = cJSON_GetObjectItemCaseSensitive(root, "protocol");
  model_copy(model->event_source, sizeof(model->event_source),
             model_string(protocol, "event_source"));
  object = cJSON_GetObjectItemCaseSensitive(protocol, "mqtt_connected");
  model->mqtt_connected = cJSON_IsTrue(object);

  devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
  homes = cJSON_GetObjectItemCaseSensitive(root, "homes");
  scenes = cJSON_GetObjectItemCaseSensitive(root, "scenes");
  if (!cJSON_IsArray(devices) || !cJSON_IsArray(homes) ||
      !cJSON_IsArray(scenes))
    {
      cJSON_Delete(root);
      return -EBADMSG;
    }

  cJSON_ArrayForEach(object, devices)
    {
      struct home_panel_device_s *device;

      if (model->device_count >= HOME_PANEL_MAX_DEVICES)
        {
          break;
        }

      device = &model->devices[model->device_count++];
      model_parse_device(object, device);
      model->online_count += device->online ? 1 : 0;
    }

  cJSON_ArrayForEach(object, homes)
    {
      cJSON *room;

      rooms = cJSON_GetObjectItemCaseSensitive(object, "rooms");
      if (!cJSON_IsArray(rooms))
        {
          continue;
        }

      cJSON_ArrayForEach(room, rooms)
        {
          struct home_panel_room_s *target;
          cJSON *dids;

          if (model->room_count >= HOME_PANEL_MAX_ROOMS)
            {
              break;
            }

          target = &model->rooms[model->room_count++];
          model_copy(target->name, sizeof(target->name),
                     model_string(room, "name"));
          dids = cJSON_GetObjectItemCaseSensitive(room, "dids");
          target->device_count = cJSON_IsArray(dids) ?
                                 cJSON_GetArraySize(dids) : 0;
        }
    }

  for (index = 0; index < model->device_count; index++)
    {
      struct home_panel_device_s *device = &model->devices[index];
      struct home_panel_room_s *room;

      if (strcmp(device->type, "environment-sensor") != 0)
        {
          continue;
        }

      room = model_find_room(model, device->room);
      if (room == NULL)
        {
          continue;
        }

      room->has_temperature = device->has_temperature;
      room->temperature = device->temperature;
      room->has_humidity = device->has_humidity;
      room->humidity = device->humidity;
    }

  cJSON_ArrayForEach(object, scenes)
    {
      struct home_panel_scene_s *scene;

      if (model->scene_count >= HOME_PANEL_MAX_SCENES)
        {
          break;
        }

      scene = &model->scenes[model->scene_count++];
      model_copy(scene->id, sizeof(scene->id),
                 model_string(object, "scene_id"));
      model_copy(scene->name, sizeof(scene->name),
                 model_string(object, "name"));
    }

  cJSON_Delete(root);
  return 0;
}
