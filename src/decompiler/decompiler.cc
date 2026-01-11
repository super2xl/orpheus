// Ghidra Decompiler Wrapper Implementation
// Copyright (C) 2025 Orpheus Project
// GPL-3.0 License

#include "decompiler.hh"

// Conditionally include Ghidra headers when available
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
#include "libdecomp.hh"
#include "dma_arch.hh"
#include "funcdata.hh"
#include "printc.hh"
#include "type_injector.hh"
#include <sstream>
#endif

namespace orpheus {

// Implementation structure (PIMPL pattern to hide Ghidra types)
struct Decompiler::Impl {
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    DMAArchitecture* architecture = nullptr;
    ghidra::Funcdata* current_function = nullptr;
    std::ostringstream output_stream;
#endif
};

Decompiler::Decompiler()
    : impl_(std::make_unique<Impl>())
{
}

Decompiler::~Decompiler()
{
    Shutdown();
}

bool Decompiler::Initialize(const DecompilerConfig& config)
{
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    if (initialized_) {
        Shutdown();
    }

    config_ = config;

    try {
        // Initialize the decompiler library with SLEIGH spec path
        if (!config.sleigh_spec_path.empty()) {
            ghidra::startDecompilerLibrary(config.sleigh_spec_path.c_str());
        } else {
            // Try to find specs in common locations
            std::vector<std::string> search_paths;
            // Add Orpheus-specific paths here
            ghidra::startDecompilerLibrary(search_paths);
        }

        initialized_ = true;
        return true;
    }
    catch (const ghidra::LowlevelError& e) {
        last_error_ = "Decompiler init failed: " + std::string(e.explain);
        return false;
    }
    catch (const std::exception& e) {
        last_error_ = "Decompiler init failed: " + std::string(e.what());
        return false;
    }
#else
    (void)config;
    last_error_ = "Ghidra decompiler not compiled in";
    return false;
#endif
}

void Decompiler::Shutdown()
{
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    if (initialized_) {
        if (impl_->architecture) {
            delete impl_->architecture;
            impl_->architecture = nullptr;
        }
        ghidra::shutdownDecompilerLibrary();
        initialized_ = false;
    }
#endif
    memory_callback_ = nullptr;
}

void Decompiler::SetMemoryCallback(DMAReadCallback callback)
{
    memory_callback_ = std::move(callback);
}

DecompileResult Decompiler::DecompileFunction(uint64_t address,
                                               const std::string& function_name,
                                               const std::string& this_type,
                                               uint32_t max_instructions)
{
    DecompileResult result;
    result.entry_point = address;
    result.function_name = function_name.empty()
        ? "func_" + std::to_string(address)
        : function_name;

#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    if (!initialized_) {
        result.error = "Decompiler not initialized";
        return result;
    }

    if (!memory_callback_) {
        result.error = "No memory callback configured";
        return result;
    }

    try {
        // Setup architecture if not done
        if (!impl_->architecture) {
            if (!SetupArchitecture()) {
                result.error = last_error_;
                return result;
            }
        }

        // Set max_instructions limit if specified
        // This controls the "Flow exceeded maximum allowable instructions" limit
        if (max_instructions > 0) {
            impl_->architecture->max_instructions = max_instructions;
        } else {
            // Reset to default (100000)
            impl_->architecture->max_instructions = 100000;
        }

        // Find or create function at address
        ghidra::Address addr(impl_->architecture->getDefaultCodeSpace(), address);
        ghidra::Scope* scope = impl_->architecture->symboltab->getGlobalScope();

        // Query existing function or create new one
        ghidra::Funcdata* fd = scope->queryFunction(addr);
        if (!fd) {
            // Try to create a new function at this address
            // This requires the function to be defined in the symbol table first
            std::string fname = result.function_name;
            scope->addFunction(addr, fname);
            fd = scope->queryFunction(addr);
        }

        if (!fd) {
            result.error = "Could not create function at address";
            return result;
        }

        impl_->current_function = fd;

        // If this_type is specified, verify it exists and add to warnings
        // Note: Full prototype injection is complex in Ghidra's API
        // For now, we verify the type exists and note it for reference
        if (!this_type.empty() && type_injector_) {
            ghidra::Datatype* ptr_type = type_injector_->GetPointerType(this_type);
            if (ptr_type) {
                // Type exists in the factory - note it in warnings for reference
                result.warnings.push_back("this_type available: " + this_type + "* (use for manual analysis)");

                // The injected types mean that any offsets that match schema fields
                // will be recognized if the decompiler can infer the type through
                // data flow analysis. For explicit typing, use the Ghidra GUI.
            } else {
                result.warnings.push_back("this_type '" + this_type + "' not found in schema");
            }
        }

        // Perform decompilation using Action framework
        if (!fd->isProcStarted()) {
            // Reset and perform analysis actions
            impl_->architecture->allacts.getCurrent()->reset(*fd);
            impl_->architecture->allacts.getCurrent()->perform(*fd);
        }

        // Capture C output
        result.c_code = CaptureOutput(fd);
        result.success = true;
    }
    catch (const ghidra::LowlevelError& e) {
        result.error = "Decompile error: " + std::string(e.explain);
    }
    catch (const std::exception& e) {
        result.error = "Decompile error: " + std::string(e.what());
    }
#else
    (void)this_type;
    result.error = "Ghidra decompiler not compiled in";
#endif

    return result;
}

DecompileResult Decompiler::DecompileRange(uint64_t start, uint64_t end)
{
    (void)end;
    // For now, treat as a single function at start
    // More sophisticated range handling would analyze the region
    return DecompileFunction(start, "range_" + std::to_string(start));
}

std::vector<std::string> Decompiler::GetAvailableProcessors() const
{
    std::vector<std::string> processors;
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    // Would enumerate from SleighArchitecture capability list
    processors.push_back("x86:LE:32:default");
    processors.push_back("x86:LE:64:default");
    // Add more as SLEIGH specs are available
#endif
    return processors;
}

bool Decompiler::SetupArchitecture()
{
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    if (!memory_callback_) {
        last_error_ = "No memory callback configured";
        return false;
    }

    try {
        // Build architecture string based on config
        std::string arch_id;
        if (config_.processor == "x86") {
            arch_id = config_.little_endian ? "x86:LE:" : "x86:BE:";
            arch_id += (config_.address_size == 64) ? "64" : "32";
            arch_id += ":default";
        } else {
            arch_id = config_.processor;
        }

        // Create DMA-based architecture
        impl_->architecture = new DMAArchitecture(arch_id, &std::cerr);

        // Configure the architecture with our DMA callback
        impl_->architecture->setDMACallback(memory_callback_);

        // Initialize the architecture
        ghidra::DocumentStorage store;
        impl_->architecture->init(store);

        return true;
    }
    catch (const ghidra::LowlevelError& e) {
        last_error_ = "Architecture setup failed: " + std::string(e.explain);
        if (impl_->architecture) {
            delete impl_->architecture;
            impl_->architecture = nullptr;
        }
        return false;
    }
    catch (const std::exception& e) {
        last_error_ = "Architecture setup failed: " + std::string(e.what());
        if (impl_->architecture) {
            delete impl_->architecture;
            impl_->architecture = nullptr;
        }
        return false;
    }
#else
    last_error_ = "Ghidra decompiler not compiled in";
    return false;
#endif
}

std::string Decompiler::CaptureOutput(ghidra::Funcdata* fd)
{
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    impl_->output_stream.str("");
    impl_->output_stream.clear();

    // Get the print language (C by default)
    ghidra::PrintLanguage* printer = impl_->architecture->print;
    printer->setOutputStream(&impl_->output_stream);

    // Print the function as C
    printer->docFunction(fd);

    return impl_->output_stream.str();
#else
    (void)fd;
    return "";
#endif
}

// ===== CS2 Schema Type Injection Implementation =====

int Decompiler::InjectSchemaTypes(const std::vector<orpheus::dumper::SchemaClass>& schema_classes)
{
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    if (!initialized_) {
        last_error_ = "Decompiler not initialized";
        return 0;
    }

    // Ensure architecture is set up
    if (!impl_->architecture) {
        if (!SetupArchitecture()) {
            return 0;
        }
    }

    // Create type injector if needed
    if (!type_injector_) {
        type_injector_ = std::make_unique<TypeInjector>();
    }

    // Configure injector with our architecture
    type_injector_->SetArchitecture(impl_->architecture);

    // Add schema classes
    type_injector_->AddSchemaClasses(schema_classes);

    // Perform injection
    injected_type_count_ = type_injector_->InjectTypes();
    types_injected_ = (injected_type_count_ > 0);

    if (!type_injector_->GetLastError().empty()) {
        last_error_ = type_injector_->GetLastError();
    }

    return injected_type_count_;
#else
    (void)schema_classes;
    last_error_ = "Ghidra decompiler not compiled in";
    return 0;
#endif
}

void Decompiler::ClearInjectedTypes()
{
#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    if (type_injector_) {
        type_injector_->ClearSchemaClasses();
    }
    types_injected_ = false;
    injected_type_count_ = 0;

    // Note: To fully clear types from the factory, we'd need to
    // re-initialize the architecture. For now, just reset our tracking.
#endif
}

} // namespace orpheus
