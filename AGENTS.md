# LLM Agent Guidelines for Unreal Engine 5 Development

This document contains essential instructions and advice designed to help the AI agent assist effectively with the development of this Unreal Engine 5 project.

## Directives

### 1. **Always Prefer C++ (CRITICAL)**
Whenever implementing logic, creating classes, handling game mechanics, or building systems, **always strictly prefer C++ over Blueprints**. 
- Blueprints should primarily be used for configuring data (data-only blueprints), setting up UI layouts, linking visual assets (meshes, materials), or very simple high-level event wiring.
- Core game logic, systems, components, and performance-critical code must always be written in C++.

### 2. Look at the development state at `Docs/development_steps.md`
Look at the development steps file and update it as the development progresses

## 3. Plan Node Default
- Enter plan mode for ANY non-trivial task (3+ steps or architectural decisions)
- If something goes sideways, STOP and re-plan immediately — don't keep pushing
- Use plan mode for verification steps, not just building
- Write detailed specs upfront to reduce ambiguity

## 4. Self-Improvement Loop
- After ANY correction from the user: update `tasks/lessons.md` with the pattern
- Write rules for yourself that prevent the same mistake
- Ruthlessly iterate on these lessons until mistake rate drops
- Review lessons at session start for relevant project

## 5. Demand Elegance (Balanced)
- For non-trivial changes: pause and ask "is there a more elegant way?"
- If a fix feels hacky: know everything you know, implement the elegant solution
- Skip this for simple, obvious fixes — don't over-engineer
- Challenge your own work before presenting it

---

# Task Management

1. **Plan First**: Write plan to `tasks/todo.md` with checkable items  
2. **Verify Plans**: Check in before starting implementation  
3. **Track Progress**: Mark items complete as you go  
4. **Explain Changes**: High-level summary at each step  
5. **Document Results**: Add review section to `tasks/todo.md`  
6. **Capture Lessons**: Update `tasks/lessons.md` after corrections  

---

# Core Principles

- **Simplicity First**: Make every change as simple as possible. Impact minimal code  
- **No Laziness**: Find root causes. No temporary fixes. Senior developer standards  
- **Minimal Impact**: Changes should only touch what's necessary. Avoid introducing bugs