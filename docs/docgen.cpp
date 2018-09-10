#include <limits.h>
#include <stdlib.h>

#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Job.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Host.h>
#include <iostream>
#include <string_view>
#include <memory>
class DocConsumer : public clang::ASTConsumer {
private:
	clang::ASTContext *ctx;
public:
	void Initialize(clang::ASTContext &Context) override {
		ctx = &Context;
	}
	bool HandleTopLevelDecl(clang::DeclGroupRef dg) override {
		if (!dg.isSingleDecl())
			return true;
		auto d = dg.getSingleDecl();
		if (!d->isFunctionOrFunctionTemplate())
			return true;
		auto f = d->getAsFunction();
		auto loc = f->getLocation();
		auto floc = clang::FullSourceLoc(loc, ctx->getSourceManager());
		if (floc.getFileID() != ctx->getSourceManager().getMainFileID())
			return true;
		auto fe = floc.getFileEntry();
		std::cerr << (fe ? fe->getName().str() : "not-a-file") << ":" << f->getName().str() << "\n";

		auto comment = ctx->getRawCommentForAnyRedecl(f);
		if (comment != nullptr)
			std::cerr << "//" << comment->getRawText(ctx->getSourceManager()).str() << "\n";
		return true;
	}
};
class TestAction : public clang::ASTFrontendAction {
protected:
	std::unique_ptr<clang::ASTConsumer> CreateASTConsumer (clang::CompilerInstance &ci, StringRef file) {
		return std::make_unique<DocConsumer>();
	}
};
int main(int argc, char **argv) {
	char *rpath = realpath(argv[1], NULL);
	std::cerr << "realpath " << rpath << "\n";
	std::string err;
	auto db = clang::tooling::CompilationDatabase::loadFromDirectory(".", err);
	clang::FrontendInputFile
	    input(rpath,
	          clang::InputKind(clang::InputKind::Language::C,
	                           clang::InputKind::Format::Source));

	clang::CompilerInstance ci;
	ci.createDiagnostics(); // create DiagnosticsEngine
	if (db != nullptr) {
		for(auto f: db->getAllFiles()) {
			char *dbpath = realpath(f.c_str(), NULL);
			std::cerr << dbpath << "\n";
			if (strcmp(dbpath, rpath) != 0) {
				free(dbpath);
				continue;
			}

			free(dbpath);
			auto args = db->getCompileCommands(f);
			std::cerr << args.size() << "\n";
			if (args.size()) {
				std::vector<const char *> cargs;
				cargs.reserve(args[0].CommandLine.size()+3);
				for(size_t i = 0; i < args[0].CommandLine.size(); i++) {
					std::string_view cmd = args[0].CommandLine[i];
					if (cmd == "-fplan9-extensions" || cmd == "ccache")
						continue;
					std::cerr << cmd << "\n";
					cargs.push_back(args[0].CommandLine[i].c_str());
				}
				cargs.push_back("-v");
				cargs.push_back("-fsyntax-only");
				cargs.push_back("-working-directory=" BUILD_DIR);

				auto d = clang::driver::Driver("/usr/bin/clang",
				    llvm::sys::getDefaultTargetTriple(), ci.getDiagnostics());
				auto c = std::unique_ptr<clang::driver::Compilation>
				    (d.BuildCompilation(cargs));
				auto job = c->getJobs().begin();

				auto driver_args = job->getArguments();
				for(auto a: driver_args)
					std::cerr << "ARG: " << a << "\n";
				clang::CompilerInvocation::CreateFromArgs(ci.getInvocation(),
				    driver_args.data(), driver_args.data()+driver_args.size(),
				    ci.getDiagnostics());
				break;
			}
		}
	}
	free(rpath);

	auto& langopts = ci.getLangOpts();
	langopts.CPlusPlus = false;

	auto to = std::make_shared<clang::TargetOptions>();
	to->Triple = llvm::sys::getDefaultTargetTriple();
	auto ti = clang::TargetInfo::CreateTargetInfo(ci.getDiagnostics(), to);
	ci.setTarget(ti);
	ci.createFileManager();  // create FileManager
	ci.createSourceManager(ci.getFileManager()); // create SourceManager
	ci.createPreprocessor(clang::TranslationUnitKind::TU_Complete);

	ci.getFrontendOpts().ASTDumpDecls = true;
	TestAction tact;
	tact.BeginSourceFile(ci, input);
	tact.Execute();
	tact.EndSourceFile();
	clang::ASTDumpAction astact;
	astact.BeginSourceFile(ci, input);
	astact.Execute();
	astact.EndSourceFile();
}
