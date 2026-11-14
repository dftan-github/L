#include "iface/gen_interface.hpp"

#include <sstream>

namespace l::iface {
namespace {

void appendGenerics(std::ostringstream& out, const std::vector<ast::GenericParam>& generics) {
  if (generics.empty()) {
    return;
  }
  out << '<';
  for (size_t i = 0; i < generics.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << generics[i].name;
    if (!generics[i].constraint.empty()) {
      out << ": " << generics[i].constraint;
    }
  }
  out << '>';
}

void appendParams(std::ostringstream& out, const std::vector<ast::Param>& params) {
  out << '(';
  for (size_t i = 0; i < params.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    const ast::Param& p = params[i];
    if (!p.type_name.empty()) {
      out << p.type_name;
      if (!p.name.empty()) {
        out << ' ';
      }
    }
    out << p.name;
    if (p.is_mutable) {
      out << '!';
    }
  }
  out << ')';
}

void appendFuncSignature(std::ostringstream& out, const ast::FuncDeclStmt& fn, int indent) {
  out << std::string(static_cast<size_t>(indent), ' ');
  if (fn.is_pub) {
    out << "pub ";
  }
  out << "fun " << fn.name;
  appendGenerics(out, fn.generics);
  appendParams(out, fn.params);
  if (!fn.return_type.empty() && fn.return_type != "void") {
    out << " -> " << fn.return_type;
  }
  out << ";\n";
}

bool classHasPublicSurface(const ast::ClassDeclStmt& cls) {
  if (cls.is_pub) {
    return true;
  }
  for (const auto& f : cls.fields) {
    if (f && f->is_pub) {
      return true;
    }
  }
  for (const auto& m : cls.methods) {
    if (m && m->is_pub && !m->is_compiler_generated) {
      return true;
    }
  }
  return false;
}

bool shouldSkipFunc(const ast::FuncDeclStmt& fn) {
  if (fn.is_extern || fn.is_compiler_generated) {
    return true;
  }
  if (fn.name == "__compile_time_handler" || fn.name == "__on_class_parsed" ||
      fn.name == "__init" || fn.name == "__deinit") {
    return true;
  }
  return false;
}

void emitClass(std::ostringstream& out, const ast::ClassDeclStmt& cls) {
  // .li is the public API surface — always emit as pub class when exported.
  out << "pub class " << cls.name;
  appendGenerics(out, cls.generics);
  if (!cls.interfaces.empty()) {
    out << " : ";
    for (size_t i = 0; i < cls.interfaces.size(); ++i) {
      if (i != 0) {
        out << ", ";
      }
      out << cls.interfaces[i];
    }
  }
  out << " {\n";

  for (const auto& field : cls.fields) {
    if (!field || !field->is_pub) {
      continue;
    }
    // Logical field name only (physical trailing _ is compiler-hidden).
    out << "    ";
    if (field->is_delegate) {
      out << "delegate " << field->type_name << "\n";
      continue;
    }
    if (field->is_pub) {
      out << "pub ";
    }
    out << field->type_name << ' ' << field->name << "\n";
  }

  for (const auto& method : cls.methods) {
    if (!method || !method->is_pub || shouldSkipFunc(*method)) {
      continue;
    }
    appendFuncSignature(out, *method, 4);
  }

  out << "}\n\n";
}

void emitInterface(std::ostringstream& out, const ast::InterfaceDeclStmt& iface) {
  out << "pub interface " << iface.name << " {\n";
  for (const auto& method : iface.methods) {
    if (!method || shouldSkipFunc(*method)) {
      continue;
    }
    // Interface methods are implicitly public signatures.
    out << "    fun " << method->name;
    appendGenerics(out, method->generics);
    appendParams(out, method->params);
    if (!method->return_type.empty() && method->return_type != "void") {
      out << " -> " << method->return_type;
    }
    out << "\n";
  }
  out << "}\n\n";
}

}  // namespace

std::string generateInterfaceFile(const ast::Module& module, const std::string& only_module) {
  std::ostringstream out;
  out << "// Auto-generated interface (.li) — signatures only, no bodies.\n\n";

  auto in_target_module = [&](const std::string& mod) {
    return only_module.empty() || mod.empty() || mod == only_module;
  };

  for (const ast::StmtPtr& stmt : module.stmts) {
    if (!stmt) {
      continue;
    }
    switch (stmt->kind()) {
      case ast::StmtKind::ClassDecl: {
        const auto& cls = static_cast<const ast::ClassDeclStmt&>(*stmt);
        if (!in_target_module(cls.module_name) || !classHasPublicSurface(cls)) {
          break;
        }
        emitClass(out, cls);
        break;
      }
      case ast::StmtKind::InterfaceDecl: {
        const auto& iface = static_cast<const ast::InterfaceDeclStmt&>(*stmt);
        if (!in_target_module(iface.module_name) || !iface.is_pub) {
          break;
        }
        emitInterface(out, iface);
        break;
      }
      case ast::StmtKind::FuncDecl: {
        const auto& fn = static_cast<const ast::FuncDeclStmt&>(*stmt);
        if (!in_target_module(fn.module_name) || !fn.is_pub || shouldSkipFunc(fn) ||
            fn.is_method) {
          break;
        }
        appendFuncSignature(out, fn, 0);
        out << '\n';
        break;
      }
      case ast::StmtKind::Directive:
      case ast::StmtKind::Import:
      case ast::StmtKind::ExternModule:
      case ast::StmtKind::VarDecl:
      case ast::StmtKind::Assign:
      case ast::StmtKind::Block:
      case ast::StmtKind::ExprStmt:
        // Strip: LCMD, imports, C bindings, private/module exec, comments (never in AST).
        break;
      default:
        break;
    }
  }

  return out.str();
}

}  // namespace l::iface
