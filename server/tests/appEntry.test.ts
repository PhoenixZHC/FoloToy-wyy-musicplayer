import { expect, test } from "vitest";
import { isMainModule } from "../src/app.js";

test("detects Windows file URL as the current entry module", () => {
  expect(
    isMainModule(
      "file:///E:/code/folotoy-wangyiyun/server/dist/app.js",
      "E:\\code\\folotoy-wangyiyun\\server\\dist\\app.js",
    ),
  ).toBe(true);
});
