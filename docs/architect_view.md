# Architect's View: Design Decisions & Rationale

When a technical reviewer asks "How did you do it?" or "How can you say you *architected* this?", they aren't looking for a list of C functions. They want to see **Design Intent**, **Trade-offs**, and **Systems Thinking**.

## 1. The "How did you do it?" Roadmap (The 5 Steps)

### Step 1: Requirements & Platform Mapping
*   **Method:** Started with the physical constraints—we needed sub-millisecond response to grid spikes to prevent hardware damage. 
*   **The Decide:** I ruled out Embedded Linux for the edge-node due to non-deterministic latency and high BOM cost. I selected the **STM32F407** specifically for its hardware FPU and multiple DMA streams.

### Step 2: Task Topology & Prioritization
*   **Method:** Designed a **3-Task Prioritized Topology**.
*   **The Decouple:** I separated **IO (Task_Poll)** from **Logic (Task_Ctrl)** from **Network (Task_Net)**. This ensures that a slow MQTT TLS handshake in the network task can never block the safety-critical inverter control loop.

### Step 3: Hardware Offloading Strategy
*   **Method:** Ruthlessly offloaded CPU-intensive tasks to silicon.
*   **The Tech:** Implemented **DMA with UART IDLE interrupts** for Modbus and used the **WIZnet W5500** for hardware TCP/IP. This keeps the CPU at ~5% load, leaving the rest for complex peak-shaving math.

### Step 4: Inter-Task Communication (IPC) Design
*   **Method:** Enforced a **"Zero Shared Memory"** policy.
*   **The Pattern:** Used FreeRTOS Queues (CMSIS-RTOS v2) to pass `SystemState_t` structs by value. This structurally eliminated **Priority Inversion** and **Race Conditions** without needing a single Mutex.

### Step 5: Reliability & Safety Layer
*   **Method:** Designed for 10-year field stability.
*   **The Guardrails:** Implemented an Independent Watchdog (IWDG) refreshed only by the high-priority poll task, enabled **Stack Watermarking**, and enforced a **Zero-Malloc** static allocation policy.

---

## 2. "How can you say you ARCHITECTED this?"

You "architected" it because you made the high-level decisions that determined the system's character:

1.  **Selection of OS and Abstraction**: You chose **CMSIS-RTOS v2.1.3** over native **FreeRTOS V10.3.1** to ensure future portability.
2.  **Memory Architecture**: You decided to map the control task stack to **CCM RAM** to optimize the FPU pipeline.
3.  **Communication Protocol Design**: You defined the `SystemState_t` contract between hardware and cloud.
4.  **Error Recovery Model**: You designed the "Ghost Connection" detection and hardware reset recovery for the W5500.
5.  **Deployment Strategy**: You defined the Flash partitioning for the Bank A/B OTA update mechanism.
6.  **Toolchain ROI & Compliance**: You balanced capital expenditure (Capex) by selecting a Free/Open-Source build stack (GCC/CubeIDE) while justifying the investment in professional debugging (SEGGER J-Link/SystemView) to reduce engineering time.
7. **Functional Safety (IEC 62061 SIL-2) Rationale**: In industrial EMS, software is safety-critical. The RTOS architecture follows **SIL-2 design patterns** (Independent Watchdogs, Deterministic Preemption, Static Allocation) to satisfy IEC 62061 requirements. We ensure that a network failure never compromises the "Safe State" of the battery contactors.7. **Functional Safety (IEC 62061) vs. Cybersecurity (IEC 62443)**: We employ a **Split-Integrity Architecture**. High-level Linux logic (`ems-app`) focuses on **IEC 62443** security. All safety-critical hardware enforcement is offloaded to the STM32 RTOS (`ems-mini-rtos`), which follows **IEC 62061 SIL-2** principles to act as a deterministic "Safety Supervisor," protecting the battery assets even if the Linux host is compromised.
---

## 3. Technical Implementation Deep-Dive

### A. How DMA Callbacks "Avoid" the RTOS Scheduler
**The Question:** *"You say your DMA callback logic avoids the scheduler. What does that mean technically?"*

**The Answer:** 
In a typical RTOS, if you wait for data in a loop, you are subject to the **SysTick** (usually 1ms). If data arrives at 0.1ms, you might wait until 1.0ms for the kernel to wake up and check. 
*   **Our Method:** By using the **HAL_UART_TxCpltCallback** and **HAL_UARTEx_RxEventCallback**, we are operating in **Hardware Interrupt Context**. 
*   **The Difference:** An interrupt has higher priority than the RTOS Kernel itself. The silicon hardware triggers the CPU to jump *immediately* to the ISR address, bypassing the "Ready List" and the 1ms tick. 
*   **The Result:** We toggle the RS-485 direction pin nanoseconds after the last bit leaves the register. If we waited for a task to wake up and do it, the slave device would already be transmitting, causing a bus collision.

### B. Why "Queues-Only" Eliminates Priority Inversion
**The Question:** *"Why are Mutexes banned, and how do Queues prevent Priority Inversion?"*

**The Answer:**
**Priority Inversion** happens when a High-priority task (H) is blocked waiting for a Mutex held by a Low-priority task (L), while a Medium-priority task (M) runs and prevents (L) from finishing. 
*   **Our Method:** We use **Asynchronous Queues** instead of shared memory protected by Mutexes.
*   **The Logic:** With a Queue, Task (H) never "waits" for Task (L) to release a lock. Instead, Task (H) sleeps on a kernel event. The moment Task (L) calls `osMessageQueuePut`, the data is copied and the kernel instantly switches to Task (H). 
*   **The Result:** There is no shared ownership of a resource, so there is no scenario where a lower-priority task can "hold hostage" a high-priority task's execution.

### C. i.MX93 (Linux Threads) vs. STM32 (RTOS) Determinism
**The Question:** *"Linux uses threads too. Isn't that also deterministic?"*

**The Answer:**
No. Standard Linux (even with `pthreads`) is **Fair-Share**, while an RTOS is **Prioritized-Preemptive**.
*   **Linux (Soft Real-Time):** The Linux kernel has vast complex layers (Virtual Memory, Context Switching, GC, Drivers). Even with high priority, a thread can be delayed by a spin-lock in the kernel or a page fault. Latency is measured in **milliseconds** and has high "jitter" (variance).
*   **RTOS (Hard Real-Time):** There is no virtual memory or complex OS background activity. When a higher-priority task is ready, it runs **instantly** (latency measured in **microseconds**). 
*   **The Verdict:** You use the **i.MX93** for heavy data processing (MQTT, JSON, Database) where throughput matters, and the **STM32** for the physical control loop where a 1ms delay could cause an electrical trip or damage a battery rack.

---

## 4. The "Natural" Response Script

**Technical Question:** *"I see you architected a deterministic RTOS controller. How did you approach that and why do you consider yourself the architect?"*

**Your Response:**
> "I approached it by first identifying that our previous super-loop design had too much jitter for industrial power balancing. I architected the migration to a **multi-task RTOS** by designing a prioritized thread topology. I made the call to use **DMA-based I/O offloading** to keep the CPU free for the control algorithms, and I established the **Zero-Malloc and Queue-based IPC templates** that the rest of the team followed. My role wasn't just writing drivers; it was defining the hardware-software contract, selecting the memory-protection strategies (like CCM RAM usage), and designing the A/B fail-safe OTA update logic."

---

## 4. To-Do / Deep-Dive Items for you
- [ ] **Review the DMA callback logic** in `main.c`—be ready to explain how it avoids the RTOS scheduler.
- [ ] **Refresh on Priority Inversion**—be ready to explain how your Queue-only design makes it impossible.
- [ ] **Flash Mapping Details**—know your Flash sector sizes (Sector 0-3 for bootloader, etc.).
