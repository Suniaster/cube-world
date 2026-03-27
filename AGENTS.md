# LLM Agent Guidelines for Unreal Engine 5 Development

This document contains essential instructions and advice designed to help the AI agent assist effectively with the development of this Unreal Engine 5 project.

## Directives

### 1. **Always Prefer C++ (CRITICAL)**
Whenever implementing logic, creating classes, handling game mechanics, or building systems, **always strictly prefer C++ over Blueprints**. 
- Blueprints should primarily be used for configuring data (data-only blueprints), setting up UI layouts, linking visual assets (meshes, materials), or very simple high-level event wiring.
- Core game logic, systems, components, and performance-critical code must always be written in C++.

## 2. Plan Node Default
- Enter plan mode for ANY non-trivial task (3+ steps or architectural decisions)
- If something goes sideways, STOP and re-plan immediately — don't keep pushing
- Use plan mode for verification steps, not just building
- Write detailed specs upfront to reduce ambiguity

## 3. Demand Elegance (Balanced)
- For non-trivial changes: pause and ask "is there a more elegant way?"
- If a fix feels hacky: know everything you know, implement the elegant solution
- Skip this for simple, obvious fixes — don't over-engineer
- Challenge your own work before presenting it