# LLM Agent Guidelines for Unreal Engine 5 Development

This document contains essential instructions and advice designed to help the AI agent assist effectively with the development of this Unreal Engine 5 project.

## Core Directives

### 1. **Always Prefer C++ (CRITICAL)**
Whenever implementing logic, creating classes, handling game mechanics, or building systems, **always strictly prefer C++ over Blueprints**. 
- Blueprints should primarily be used for configuring data (data-only blueprints), setting up UI layouts, linking visual assets (meshes, materials), or very simple high-level event wiring.
- Core game logic, systems, components, and performance-critical code must always be written in C++.