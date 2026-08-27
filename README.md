# SmartCourseScheduler
## Yehan WANG, ECNU

<div align="center">
    <img src="https://cdn.jsdelivr.net/gh/VisionNext100/VisionNext100.github.io@main/public/images/projects/project-coursesched.png" width="800" alt="SmartCourseScheduler">
</div>

### Ⅰ Overview
SmartCourseScheduler is an intelligent academic course enrollment planning and scheduling system implemented in C++17 and the Qt6 framework. Designed to tackle the multi-constraint scheduling problem, this system automates optimal 8-semester timetable generation while complying with mandatory graduation credit boundaries, directed prerequisite dependency paths, specific time-slot exclusions, and sequential semester continuations. 

The architecture features interactive graphical components for advanced restriction mapping, paired with optimized native C++ and Python verification processors to handle automated, offline structural legality audits.

### Ⅱ Structure
```text
SmartCourseScheduler/
├── README.md         # Description, implementation details, and compilation guide
├── LICENSE           # MIT open-source license (pulled from GitHub)
├── .gitignore        # Precise rules to ignore build files and schedule.json
├── CMakeLists.txt    # CMake configuration script for compiling the system
├── main.cpp          # Application initialization bootstrap
├── mainwindow.h      # Scheduler state machines and GUI slot declarations
├── mainwindow.cpp    # Core algorithm implementation (Bitmasks & Fitness-Scoring)
├── mainwindow.ui     # Visual layout XML for the 8-semester course grids
├── course.json       # Core input dataset defining 30 standard courses
├── checker.py        # Automated Python verification routine using combinations
├── checker.cpp       # Standalone C++ validation pipeline source
├── nlohmann/         # Header-only modern JSON library components for checker
└── docs/             # Academic documentation assets
    ├── 智能选课系统-新版.pdf
    └── 《智能选课系统》项目报告书.pdf  
```

### Ⅲ Implementation
- **Bitmask Time-Slot Matrix:** To verify calendar scheduling overlaps with deterministic execution speeds, weekly schedules (7 days $\times$ 13 slots) are mapped into compressed integer arrays where individual course periods act as active binary bit-flags. Conflict detection between concurrent offerings evaluates a strict bitwise AND verification: `(offeringA.times[day] & offeringB.times[day]) != 0`.  
- **DAG Prerequisite Controls:** Chronological integrity is strictly guarded by evaluating curriculum requirements as an DAG. The loading sequence filters candidate semesters via a sequential check `isPrerequisiteSatisfied()`, guaranteeing that advanced sequential streams are dynamically locked out from allocation blocks until all mandatory predecessor classes are completed in an earlier term.  
- **Multidimensional Fitness-Scoring:** To achieve robust scheduling limits (supporting inputs ranging up to 300+ total credits) without causing semester overload, the kernel integrates a localized adaptation scorer. Courses are dynamically weighted based on credit density, curriculum type (compulsory vs. elective), and term flexibility, filtering them through a single-pass optimization sweep that strictly honors the semester caps.  
- **Consecutive-Term Continuation:** Complex program pathways that require absolute consecutive semester pairing (e.g., Mathematical Analysis I & II) are prioritized via independent structural parsing. The routing engine pairs these consecutive constraints beforehand using an adjacency tracker map, safely locking down adjacent semester slots prior to general curriculum generation sweeps. 
- **Automated Validation Closures:** Independent checking nodes are packed into the environment to verify output parameters. By parsing runtime data structures using specialized dictionary lookups, the validation processors execute deep combination intersections to audit timeline overlap anomalies, missing prerequisites, or index formatting errors.

### Ⅳ Compilation
1\. **Prerequisites**
- A C++ compiler supporting modern C++ standards.
- The Qt 6 framework, specifically the Widgets module.
- CMake (minimum version 3.16).  
  
2\. Open your terminal environment or Git Bash, move into the `SmartCourseScheduler/` folder, and trigger compiler builds by executing:
```shell
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```
3\. **Running the Application**
- Launch the compiled desktop program via your filesystem runner.
- Click "导入课程" to map the embedded `course.json` data array into the dynamic tabular data model view.  
- Set up target credit caps and excluded hours by clicking "设置约束" before triggering the "生成排课" action thread. 
- Run the automated legal checkers inside your test console to confirm compliance with all assignment rules:
  ```shell
  # To check via the native C++ testing pipeline
  g++ -std=c++17 checker.cpp -I. -o system_checker
  ./system_checker

  # To check via the parallel Python evaluation tool
  python checker.py
  ```
