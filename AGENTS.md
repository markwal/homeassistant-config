# AGENTS.md

This repository is the configuration directory for a Home Assistant installation. Treat it as live home automation configuration: changes can affect devices, alerts, sensors, automations, and local network integrations.

## Repository Layout

- `configuration.yaml` is the main Home Assistant configuration entry point.
- `packages/` contains Home Assistant packages loaded with `homeassistant: packages: !include_dir_named packages`.
- `automations.yaml`, `scripts.yaml`, and `scenes.yaml` are included from `configuration.yaml`.
- `blueprints/` contains Home Assistant automation and script blueprints.
- `esphome/` contains ESPHome device configuration.
- `themes/` may be referenced by `frontend: themes: !include_dir_merge_named themes`, even if it is not currently present.

## Working Guidelines

- Keep changes scoped and conservative. Home Assistant config often represents physical devices and household routines, so avoid unrelated cleanup or broad refactors.
- Preserve existing include patterns. Prefer adding cohesive integrations or entity groups under `packages/` when the config is logically self-contained.
- Do not remove `default_config:` unless explicitly asked; it enables many standard Home Assistant integrations.
- Do not commit secrets, access tokens, passwords, API keys, precise location data, or private network credentials. Use `secrets.yaml` references such as `!secret some_key` when sensitive values are needed.
- Be careful with entity IDs, MQTT topics, `unique_id` values, device classes, units, and state classes. These affect dashboards, recorder history, statistics, automations, and UI customizations.
- For MQTT entities, keep topic names, payloads, templates, and availability/expiration behavior aligned with the publishing device or service.
- For automations, prefer explicit triggers, conditions, and clear entity IDs. Avoid changes that could repeatedly trigger notifications, lights, locks, garage doors, HVAC, generators, or other physical systems without careful review.
- For ESPHome configs, avoid changing pins, board types, substitutions, API encryption keys, OTA settings, Wi-Fi settings, or device names unless the requested change requires it.

## Style

- Use YAML that is valid for Home Assistant, not just generic YAML.
- Keep indentation consistent at two spaces.
- Prefer readable names and comments where they explain device-specific behavior, external dependencies, or non-obvious templates.
- Keep package files focused around one integration, device group, room, or functional area.
- Avoid churn in generated or UI-managed files unless the task specifically involves those files.

## Validation

When possible, validate configuration before considering work complete:

- Run Home Assistant's config check if the environment provides Home Assistant:

  ```powershell
  hass --script check_config -c .
  ```

- If Home Assistant is containerized, use the equivalent command for the local deployment, for example:

  ```powershell
  docker exec homeassistant python -m homeassistant --script check_config -c /config
  ```

- For ESPHome changes, validate the affected file if ESPHome is available:

  ```powershell
  esphome config esphome/<device>.yaml
  ```

If the validation tool is unavailable, report that explicitly and still check the changed YAML for obvious syntax and Home Assistant schema issues.

## Safety Notes

- Treat automations controlling locks, doors, alarms, water valves, HVAC, electrical loads, or generator behavior as high impact.
- Do not invent entity IDs, MQTT topics, secrets, hostnames, or device capabilities. Derive them from the repository or ask for clarification.
- Avoid deleting existing `unique_id` values unless intentionally replacing an entity; changing them can orphan Home Assistant entity customizations.
- Preserve comments that document hardware quirks, external service requirements, or device-specific assumptions.
